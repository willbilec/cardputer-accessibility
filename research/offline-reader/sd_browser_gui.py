"""Two-pane Cardputer SD browser for Windows and NVDA."""

from contextlib import contextmanager
import ipaddress
from pathlib import Path, PurePosixPath
import socket
import threading
import time

import serial
import wx

from usb_sd_browser import (
    directory_entries, download_stream, exchange, find_port, remote_path, require_ok, upload_stream,
)


SHARE_PORT = 47631


class AccessibleName(wx.Accessible):
    def GetName(self, child_id):
        if child_id == 0:
            return wx.ACC_OK, self.GetWindow().GetName()
        return wx.ACC_NOT_IMPLEMENTED, ""


def expose_name(control):
    control.SetAccessible(AccessibleName(control))


class NetworkPort:
    def __init__(self, sock, reader):
        self.sock = sock
        self.reader = reader

    def write(self, data):
        self.sock.sendall(data)

    def readline(self):
        return self.reader.readline(1024)

    def read(self, size):
        return self.reader.read(size)


def discover():
    addresses = set()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
        deadline = time.monotonic() + 2
        sock.sendto(b"CARDFILES DISCOVER 1", ("255.255.255.255", SHARE_PORT))
        while (remaining := deadline - time.monotonic()) > 0:
            sock.settimeout(remaining)
            try:
                reply, (address, port) = sock.recvfrom(128)
            except socket.timeout:
                break
            if reply == b"CARDFILES WIFI 1" and port == SHARE_PORT:
                addresses.add(address)
    return sorted(addresses, key=ipaddress.IPv4Address)


@contextmanager
def connect(transport, address, code):
    if transport == "Wi-Fi":
        ip = str(ipaddress.IPv4Address(address))
        if len(code) != 8 or any(digit not in "0123456789" for digit in code):
            raise ValueError("Enter the eight-digit sharing code spoken by the Cardputer.")
        with socket.create_connection((ip, SHARE_PORT), timeout=5) as sock:
            sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
            with sock.makefile("rb") as reader:
                port = NetworkPort(sock, reader)
                require_ok(port, "AUTH " + code)
                yield from checked_session(port)
    else:
        with serial.Serial(address or find_port(), 115200, timeout=4, write_timeout=4) as port:
            port.reset_input_buffer()
            yield from checked_session(port)


def checked_session(port):
    if exchange(port, "HELLO") != "CARDFILES 1":
        raise ValueError("Open Browse SD card on the Cardputer and select the matching connection mode.")
    try:
        yield port
    finally:
        try:
            require_ok(port, "CANCEL")
        except (OSError, ValueError):
            pass


def filename(name):
    if (not name or name.endswith((".", " ")) or
            any(char in '<>:"/\\|?*' or ord(char) < 32 for char in name) or
            PurePosixPath(name).name != name):
        raise ValueError("The selected name cannot be copied safely.")
    device = name.split(".", 1)[0].rstrip().upper()
    if device in {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"} or (
        len(device) == 4 and device[:3] in {"COM", "LPT"} and device[3] in "123456789¹²³"
    ):
        raise ValueError("Windows reserves that file name.")
    return name


class FilePane:
    def __init__(self, parent, title, open_folder):
        self.entries = []
        self.layout = wx.StaticBoxSizer(wx.VERTICAL, parent, title)
        box = self.layout.GetStaticBox()
        path_label = wx.StaticText(box, label=title + " folder path")
        self.path = wx.TextCtrl(box, style=wx.TE_PROCESS_ENTER, name=title + " folder path")
        go = wx.Button(box, label="Open folder", name="Open " + title + " folder")
        list_label = wx.StaticText(box, label=title + " files")
        self.list = wx.ListBox(box, name=title + " files", style=wx.LB_SINGLE | wx.WANTS_CHARS)
        expose_name(self.path)
        expose_name(self.list)
        self.path.Bind(wx.EVT_TEXT_ENTER, open_folder)
        go.Bind(wx.EVT_BUTTON, open_folder)
        self.layout.Add(path_label, 0, wx.LEFT | wx.TOP, 4)
        self.layout.Add(self.path, 0, wx.EXPAND | wx.ALL, 4)
        self.layout.Add(go, 0, wx.ALL, 4)
        self.layout.Add(list_label, 0, wx.LEFT | wx.TOP, 4)
        self.layout.Add(self.list, 1, wx.EXPAND | wx.ALL, 4)

    def show(self, path, entries, parent):
        previous = self.selected() if self.path.GetValue() == str(path) else None
        self.path.ChangeValue(str(path))
        self.entries = sorted(entries, key=lambda item: (not item[1], item[0].casefold()))
        labels = [f"{name}, folder" if folder else f"{name}, {size:,} bytes"
                  for name, folder, size in self.entries]
        if parent:
            self.entries.insert(0, ("..", True, 0))
            labels.insert(0, "Parent folder")
        self.list.Set(labels)
        if self.entries:
            self.list.SetSelection(self.entries.index(previous) if previous in self.entries else 0)

    def selected(self):
        index = self.list.GetSelection()
        return self.entries[index] if index != wx.NOT_FOUND else None


class Browser(wx.Frame):
    def __init__(self):
        super().__init__(None, title="Cardputer SD browser", size=(920, 620))
        self.busy = False
        self.local_folder = Path.home()
        self.card_folder = PurePosixPath("/")
        self.endpoint = None
        panel = wx.Panel(self)
        layout = wx.BoxSizer(wx.VERTICAL)
        self.connection_panel = wx.Panel(panel, style=wx.TAB_TRAVERSAL)
        connection = wx.BoxSizer(wx.HORIZONTAL)
        self.connection_panel.SetSizer(connection)
        transport_label = wx.StaticText(self.connection_panel, label="Connection type")
        self.transport = wx.Choice(self.connection_panel, choices=["USB", "Wi-Fi"], name="Connection type")
        self.transport.SetSelection(0)
        self.address_label = wx.StaticText(self.connection_panel, label="COM port (optional)")
        self.address = wx.TextCtrl(self.connection_panel, name="USB COM port, optional")
        code_label = wx.StaticText(self.connection_panel, label="Sharing code")
        self.code = wx.TextCtrl(self.connection_panel, style=wx.TE_PASSWORD, name="Wi-Fi sharing code")
        self.code.SetMaxLength(8)
        for control in (self.transport, self.address, self.code):
            expose_name(control)
        for control in (transport_label, self.transport, self.address_label, self.address,
                        code_label, self.code):
            connection.Add(control, 0, wx.ALIGN_CENTER_VERTICAL | wx.ALL, 4)
        self.scan = self.button(self.connection_panel, connection, "Find Cardputers", self.scan_network)
        self.button(self.connection_panel, connection, "Connect", self.connect_card)
        layout.Add(self.connection_panel, 0, wx.EXPAND)
        panes = wx.BoxSizer(wx.HORIZONTAL)
        self.card = FilePane(panel, "Cardputer SD card", self.open_card_path)
        self.local = FilePane(panel, "Local computer", self.open_local_path)
        self.card.path.ChangeValue("/")
        for pane in (self.card, self.local):
            panes.Add(pane.layout, 1, wx.EXPAND | wx.ALL, 5)
            pane.list.Bind(wx.EVT_LISTBOX_DCLICK, lambda event, p=pane: self.open_selected(p))
            pane.list.Bind(wx.EVT_KEY_DOWN, lambda event, p=pane: self.list_key(event, p))
            pane.list.Bind(wx.EVT_CONTEXT_MENU, lambda event, p=pane: self.context_menu(p, event))
        layout.Add(panes, 1, wx.EXPAND)
        actions = wx.BoxSizer(wx.HORIZONTAL)
        self.button(panel, actions, "Download to local", self.get_file)
        self.button(panel, actions, "Upload to SD card", self.put_file)
        self.button(panel, actions, "New SD folder", self.make_folder)
        self.button(panel, actions, "Refresh panes", self.refresh)
        self.disconnect = self.button(panel, actions, "Disconnect", self.disconnect_card)
        self.disconnect.Hide()
        layout.Add(actions)
        progress_label = wx.StaticText(panel, label="Transfer progress")
        self.progress = wx.Gauge(panel, range=100, name="Transfer progress")
        expose_name(self.progress)
        layout.Add(progress_label, 0, wx.LEFT, 5)
        layout.Add(self.progress, 0, wx.EXPAND | wx.ALL, 5)
        status_label = wx.StaticText(panel, label="Operation status")
        self.status = wx.TextCtrl(panel, style=wx.TE_READONLY, name="Operation status")
        expose_name(self.status)
        layout.Add(status_label, 0, wx.LEFT, 5)
        layout.Add(self.status, 0, wx.EXPAND | wx.ALL, 5)
        panel.SetSizer(layout)
        self.transport.Bind(wx.EVT_CHOICE, self.change_transport)
        self.Bind(wx.EVT_CLOSE, self.close)
        self.shortcuts({
            "Switch file pane\tF6": self.switch_pane,
            "Refresh panes\tF5": self.refresh,
            "Download to local\tCtrl+D": self.get_file,
            "Upload to SD card\tCtrl+U": self.put_file,
            "Focus transfer progress\tCtrl+P": lambda event: self.progress.SetFocus(),
            "Read status\tCtrl+I": lambda event: self.status.SetFocus(),
        })
        self.change_transport(None)
        self.show_local(self.local_folder)
        self.status.ChangeValue("Open SD sharing on the Cardputer, then connect.")
        self.transport.SetFocus()

    def button(self, panel, layout, label, handler):
        button = wx.Button(panel, label=label)
        button.Bind(wx.EVT_BUTTON, handler)
        layout.Add(button, 0, wx.ALL, 4)
        return button

    def shortcuts(self, commands):
        menu = wx.Menu()
        for label, handler in commands.items():
            item = menu.Append(wx.ID_ANY, label)
            self.Bind(wx.EVT_MENU, handler, item)
        bar = wx.MenuBar()
        bar.Append(menu, "&Commands")
        self.SetMenuBar(bar)

    def close(self, event):
        if self.busy and event.CanVeto():
            event.Veto()
            self.status.SetFocus()
        else:
            event.Skip()

    def error(self, error):
        self.status.ChangeValue(str(error))
        wx.MessageBox(str(error), "Cardputer SD browser", wx.OK | wx.ICON_ERROR, self)

    def work(self, message, operation, complete):
        if self.busy:
            return
        self.busy = True
        self.transport.Disable()
        self.disconnect.Disable()
        self.status.ChangeValue(message)

        def run():
            try:
                result = operation()
            except Exception as error:
                wx.CallAfter(finish, None, error)
            else:
                wx.CallAfter(finish, result, None)

        def finish(result, error):
            self.busy = False
            self.transport.Enable()
            self.disconnect.Enable()
            if error is None:
                complete(result)
            else:
                self.error(error)

        threading.Thread(target=run, daemon=True).start()

    def change_transport(self, event):
        wifi = self.transport.GetStringSelection() == "Wi-Fi"
        label = "Cardputer IPv4 address" if wifi else "COM port (optional)"
        self.address_label.SetLabel(label)
        self.address.SetName(label)
        self.address.ChangeValue("")
        self.code.Enable(wifi)
        self.scan.Enable(wifi)
        self.address.GetParent().Layout()
        if event is not None:
            self.disconnect_card()
            self.status.ChangeValue("Choose the matching SD sharing mode on the Cardputer, then connect.")

    def disconnect_card(self, event=None):
        if self.busy:
            return
        self.endpoint = None
        self.card_folder = PurePosixPath("/")
        self.card.show(self.card_folder, [], False)
        if event is not None:
            self.code.ChangeValue("")
            self.connection_panel.Show()
            self.disconnect.Hide()
            self.connection_panel.GetParent().Layout()
            self.transport.SetFocus()
            self.status.ChangeValue("Disconnected.")

    def scan_network(self, event):
        def found(addresses):
            self.status.ChangeValue(f"Found {len(addresses)} Cardputer devices.")
            if not addresses:
                wx.MessageBox("No Cardputers replied. Enable Wi-Fi SD sharing, or enter the spoken IP address.",
                              "Find Cardputers", wx.OK | wx.ICON_INFORMATION, self)
            elif self.transport.GetStringSelection() == "Wi-Fi":
                with wx.SingleChoiceDialog(self, "Choose a Cardputer IP address", "Find Cardputers", addresses) as dialog:
                    if dialog.ShowModal() == wx.ID_OK:
                        self.address.ChangeValue(dialog.GetStringSelection())
                        self.code.SetFocus()
        self.work("Looking for Cardputers on the local network…", discover, found)

    def connect_card(self, event):
        if self.busy:
            return
        endpoint = (self.transport.GetStringSelection(), self.address.GetValue().strip(), self.code.GetValue())
        self.disconnect_card()
        self.show_card(PurePosixPath("/"), endpoint)

    def show_card(self, path, endpoint):
        def listing():
            with connect(*endpoint) as port:
                return list(directory_entries(port, str(path)))

        def loaded(entries):
            self.endpoint = endpoint
            self.connection_panel.Hide()
            self.disconnect.Show()
            self.connection_panel.GetParent().Layout()
            self.card_folder = path
            self.card.show(path, entries, path != PurePosixPath("/"))
            self.status.ChangeValue(f"Cardputer folder {path}. {len(entries)} items.")
            self.card.list.SetFocus()
        self.work(f"Reading Cardputer folder {path}…", listing, loaded)

    def open_card_path(self, event):
        if self.endpoint is None:
            self.error("Connect to the Cardputer first.")
        else:
            path = PurePosixPath("/" + self.card.path.GetValue().lstrip("/"))
            self.show_card(path, self.endpoint)

    def show_local(self, path, focus=False):
        try:
            path = path.expanduser().resolve(strict=True)
            entries = []
            for entry in path.iterdir():
                try:
                    folder = entry.is_dir()
                    entries.append((entry.name, folder, 0 if folder else entry.stat().st_size))
                except OSError:
                    continue
        except (OSError, ValueError) as error:
            self.error(error)
            return
        self.local_folder = path
        self.local.show(path, entries, path.parent != path)
        if focus:
            self.local.list.SetFocus()

    def open_local_path(self, event):
        self.show_local(Path(self.local.path.GetValue()), focus=True)

    def open_selected(self, pane):
        entry = pane.selected()
        if not entry:
            return
        if not entry[1]:
            self.transfer(pane is self.card)
            return
        name = entry[0]
        if pane is self.card:
            folder = self.card_folder
            self.show_card(folder.parent if name == ".." else folder / name, self.endpoint)
        else:
            folder = self.local_folder
            self.show_local(folder.parent if name == ".." else folder / name, focus=True)

    def list_key(self, event, pane):
        if event.GetKeyCode() in (wx.WXK_RETURN, wx.WXK_NUMPAD_ENTER):
            self.open_selected(pane)
        elif event.GetKeyCode() == wx.WXK_TAB:
            direction = wx.NavigationKeyEvent.IsBackward if event.ShiftDown() else wx.NavigationKeyEvent.IsForward
            pane.list.Navigate(direction)
        elif event.GetKeyCode() == wx.WXK_F10 and event.ShiftDown():
            self.context_menu(pane)
        else:
            event.Skip()

    def switch_pane(self, event):
        target = self.local if wx.Window.FindFocus() in (self.card.list, self.card.path) else self.card
        target.list.SetFocus()

    def context_menu(self, pane, event=None):
        if self.busy:
            return
        if event is not None and event.GetPosition() != wx.DefaultPosition:
            index = pane.list.HitTest(pane.list.ScreenToClient(event.GetPosition()))
            if index == wx.NOT_FOUND:
                return
            pane.list.SetSelection(index)
        entry = pane.selected()
        if not entry:
            return
        name, folder, _ = entry
        with wx.Menu() as menu:
            actions = []
            if folder:
                actions.append(("Open folder", lambda: self.open_selected(pane)))
            elif self.endpoint is not None:
                label = "Download to local" if pane is self.card else "Upload to SD card"
                actions.append((label, lambda: self.transfer(pane is self.card)))
            if name != "..":
                actions.extend((
                    ("Rename…", lambda: self.rename_item(pane, name)),
                    ("Delete…", lambda: self.delete_item(pane, name, folder)),
                ))
            for label, action in actions:
                item = menu.Append(wx.ID_ANY, label)
                menu.Bind(wx.EVT_MENU, lambda event, callback=action: callback(), item)
            if actions:
                pane.list.PopupMenu(menu)

    def rename_item(self, pane, name):
        with wx.TextEntryDialog(self, "Enter the new name", "Rename item", name) as dialog:
            if dialog.ShowModal() != wx.ID_OK or dialog.GetValue() == name:
                return
            try:
                target_name = filename(dialog.GetValue())
            except ValueError as error:
                self.error(error)
                return
        if pane is self.card:
            try:
                source = remote_path(str(self.card_folder / name))
                target = remote_path(str(self.card_folder / target_name))
            except ValueError as error:
                self.error(error)
                return
            self.change_card_item(f"RENAME {source} {target}", f"Renaming {name}…")
        else:
            target = self.local_folder / target_name
            try:
                if target.exists():
                    raise ValueError(f"Destination already exists: {target_name}")
                (self.local_folder / name).rename(target)
            except (OSError, ValueError) as error:
                self.error(error)
                return
            self.show_local(self.local_folder, focus=True)
            self.status.ChangeValue(f"Renamed {name} to {target_name}.")

    def delete_item(self, pane, name, folder):
        message = f"Permanently delete {name}?"
        if folder:
            message += " The folder must be empty."
        if wx.MessageBox(message, "Confirm deletion", wx.YES_NO | wx.NO_DEFAULT | wx.ICON_WARNING, self) != wx.YES:
            return
        if pane is self.card:
            try:
                target = remote_path(str(self.card_folder / name))
            except ValueError as error:
                self.error(error)
                return
            self.change_card_item(f"DELETE {target}", f"Deleting {name}…")
        else:
            path = self.local_folder / name
            try:
                if folder:
                    path.rmdir()
                else:
                    path.unlink()
            except OSError as error:
                self.error(error)
                return
            self.show_local(self.local_folder, focus=True)
            self.status.ChangeValue(f"Deleted {name}.")

    def change_card_item(self, command, message):
        endpoint = self.endpoint

        def change():
            with connect(*endpoint) as port:
                require_ok(port, command)

        self.work(message, change, lambda result: self.show_card(self.card_folder, endpoint))

    def refresh(self, event):
        self.show_local(self.local_folder)
        if self.endpoint is not None:
            self.show_card(self.card_folder, self.endpoint)

    def transfer(self, from_card):
        if self.busy:
            return
        entry = (self.card if from_card else self.local).selected()
        if self.endpoint is None or not entry or entry[1]:
            self.error("Connect to the Cardputer and select a file in the source pane.")
            return
        if not from_card and self.endpoint[0] == "Wi-Fi" and self.card_folder == PurePosixPath("/"):
            self.error("Open a shared Cardputer folder, such as Transfers, before uploading.")
            return
        try:
            name = filename(entry[0])
        except ValueError as error:
            self.error(error)
            return
        endpoint = self.endpoint
        remote = str(self.card_folder / name)
        local = self.local_folder / name

        direction = "Downloading" if from_card else "Uploading"
        self.progress.SetValue(0)
        started = time.monotonic()
        last_report = 0

        def report(done, total):
            nonlocal last_report
            now = time.monotonic()
            if done and done != total and now - last_report < 0.35:
                return
            last_report = now
            percent = min(99, done * 100 // total) if total else 0
            speed = done / max(now - started, 0.01) / 1024
            wx.CallAfter(self.report_progress, percent,
                         f"{direction} {name}. {percent} percent. {done:,} of {total:,} bytes. "
                         f"{speed:,.0f} KiB per second.")

        def copy():
            with connect(*endpoint) as port:
                if from_card:
                    download_stream(port, remote, local, report)
                else:
                    upload_stream(port, local, remote, report, usb=endpoint[0] == "USB")

        def copied(result):
            self.report_progress(100, f"Copied {name}. 100 percent.")
            if from_card:
                self.show_local(self.local_folder)
            wx.MessageBox(f"Copied {name}.", "Transfer complete", wx.OK | wx.ICON_INFORMATION, self)
            if not from_card:
                self.show_card(self.card_folder, endpoint)
        self.work(f"Copying {name}…", copy, copied)

    def report_progress(self, percent, message):
        self.progress.SetValue(percent)
        self.status.ChangeValue(message)
        wx.Accessible.NotifyEvent(wx.ACC_EVENT_OBJECT_VALUECHANGE, self.progress, wx.OBJID_CLIENT, 0)

    def get_file(self, event):
        self.transfer(True)

    def put_file(self, event):
        self.transfer(False)

    def make_folder(self, event):
        if self.endpoint is None or self.busy:
            return
        with wx.TextEntryDialog(self, "Enter the new folder name", "New SD folder") as dialog:
            if dialog.ShowModal() != wx.ID_OK:
                return
            try:
                target = self.card_folder / filename(dialog.GetValue())
            except ValueError as error:
                self.error(error)
                return
        endpoint = self.endpoint

        def create():
            with connect(*endpoint) as port:
                require_ok(port, "MKDIR " + remote_path(str(target)))
        self.work(f"Creating {target}…", create, lambda result: self.show_card(self.card_folder, endpoint))


if __name__ == "__main__":
    app = wx.App()
    frame = Browser()
    frame.Show()
    app.MainLoop()
