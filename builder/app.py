"""Keyboard-accessible Cardputer ADV firmware builder for Windows and Linux."""

from datetime import datetime
from pathlib import Path
import subprocess
import threading
from time import monotonic
from uuid import uuid4

import wx
from wx.lib.scrolledpanel import ScrolledPanel

from build import NO_CONSOLE, ROOT, build, load_modules
from catalog import STATE, cached_catalog, device_update_summary, install_sd, installed_version, refresh, source_tree, version
from flash import flash, ports, read_identity
from progress import output_events
from program_update import check_update, install_update

RECOVERY = (
    "If the Cardputer fails to start or speak, unplug USB and switch power off. "
    "Hold the G0 button on the back edge, just to the right of the power switch. "
    "Keep holding it while plugging a USB-C data cable into the computer. Release it after USB powers the device. "
    "The download port may have a different name. Close serial monitors, refresh USB ports here, "
    "select the download port, and use Flash a saved build. To restore the vendor software, "
    "select Cardputer ADV factory firmware in M5Burner. Keep the cable connected during writing and verification."
)


class Builder(wx.Frame):
    def __init__(self):
        super().__init__(None, title="Cardputer ADV Firmware Builder", size=(770, 680))
        panel = wx.Panel(self)
        layout = wx.BoxSizer(wx.VERTICAL)
        panel.SetSizer(layout)
        self.catalog = None
        self.catalog_checked_at = None
        self.local = load_modules(ROOT) if (ROOT / "builder/modules.json").is_file() and (
            ROOT / "research/offline-reader/platformio.ini").is_file() else None
        self.port_records = []
        self.actions = []

        layout.Add(wx.StaticText(panel, label="Firmware source"), 0, wx.ALL, 6)
        self.source = wx.Choice(panel, choices=(["Local source", "GitHub release"] if self.local else ["GitHub release"]),
                                name="Firmware source")
        self.source.SetSelection(0)
        self.source.Bind(wx.EVT_CHOICE, self.change_source)
        layout.Add(self.source, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 6)
        layout.Add(wx.StaticText(panel, label="Apps to include. Press Space to toggle the selected app."), 0, wx.ALL, 6)
        self.modules = wx.ListCtrl(panel, style=wx.LC_REPORT | wx.LC_SINGLE_SEL,
                                   name="Apps to include. Press Space to toggle.")
        self.native_checkboxes = self.modules.EnableCheckBoxes()
        for index, (label, width) in enumerate((("App", 225), ("Included", 80), ("Description", 430), ("Version", 85))):
            self.modules.InsertColumn(index, label, width=width)
        self.modules.Bind(wx.EVT_LIST_KEY_DOWN, self.toggle_module)
        self.modules.Bind(wx.EVT_LIST_ITEM_CHECKED, self.module_changed)
        self.modules.Bind(wx.EVT_LIST_ITEM_UNCHECKED, self.module_changed)
        if not self.native_checkboxes:
            self.modules.Bind(wx.EVT_LIST_ITEM_ACTIVATED, lambda event: self.toggle_row(event.GetIndex()))
        layout.Add(self.modules, 1, wx.EXPAND | wx.LEFT | wx.RIGHT, 6)
        self.button(panel, layout, "Select all current apps", self.select_all)
        self.screen = wx.CheckBox(panel, label="Include LCD display")
        layout.Add(self.screen, 0, wx.LEFT | wx.RIGHT | wx.TOP, 6)
        self.button(panel, layout, "Build firmware", lambda _: self.start_build("build"))

        layout.Add(wx.StaticText(panel, label="Cardputer USB port"), 0, wx.ALL, 6)
        self.port = wx.Choice(panel, name="Cardputer USB port")
        layout.Add(self.port, 0, wx.EXPAND | wx.LEFT | wx.RIGHT, 6)
        self.button(panel, layout, "Build and flash", lambda _: self.start_build("flash"))
        tools = wx.Menu()
        for label, handler in (
            ("Check for builder updates", self.update_builder),
            ("Refresh GitHub catalog", self.refresh_catalog),
            ("Refresh USB ports", self.refresh_ports),
            ("Update installed firmware", self.update_firmware),
            ("Install large partition", self.migrate),
            ("Flash a saved build", self.flash_saved),
            ("Update SD packages", self.update_sd),
        ):
            item = tools.Append(wx.ID_ANY, label)
            self.Bind(wx.EVT_MENU, handler, item)
        bar = wx.MenuBar()
        bar.Append(tools, "&Tools")
        self.SetMenuBar(bar)
        layout.Add(wx.StaticText(panel, label="Activity log"), 0, wx.ALL, 6)
        self.log = wx.TextCtrl(panel, style=wx.TE_MULTILINE | wx.TE_READONLY, name="Activity log")
        layout.Add(self.log, 1, wx.EXPAND | wx.ALL, 6)
        self.make_status = self.CreateStatusBar()
        self.busy = False
        self.dialog_open = False
        self.Bind(wx.EVT_CLOSE, self.on_close)
        self.refresh_ports()
        try:
            self.catalog = cached_catalog()
        except (OSError, ValueError, KeyError, TypeError):
            self.report("Cached catalog could not be read. Refresh the GitHub catalog.")
        self.show_modules()
        self.modules.SetFocus()
        if not self.local and not self.catalog:
            wx.CallAfter(self.refresh_catalog, None, True)
        if len(self.port_records) == 1:
            wx.CallLater(500, self.startup_device_check, self.port_records[0].device)
        if Path(__file__).with_name("program.json").is_file() or self.local:
            wx.CallLater(1500, self.update_builder, None)

    def button(self, parent, layout, label, handler):
        control = wx.Button(parent, label=label)
        control.Bind(wx.EVT_BUTTON, handler)
        layout.Add(control, 0, wx.LEFT | wx.RIGHT | wx.TOP, 6)
        self.actions.append(control)

    def selected_catalog(self):
        return self.local if self.local and self.source.GetSelection() == 0 else self.catalog

    def selected_modules(self):
        return [item["id"] for index, item in enumerate(self.native) if self.module_selected(index)]

    def update_screen_checkbox(self):
        supported = (self.selected_catalog() or {}).get("supports_screen", False)
        self.screen.SetLabel("Include LCD display" if supported else "Include LCD display (unavailable for this source)")
        self.screen.Enable(supported and not self.busy)

    def show_modules(self, retain=None):
        catalog = self.selected_catalog()
        self.update_screen_checkbox()
        self.native = [item for item in (catalog or {}).get("modules", []) if item["kind"] == "native"]
        self.modules.DeleteAllItems()
        for index, item in enumerate(self.native):
            self.modules.InsertItem(index, item["name"])
            self.modules.SetItem(index, 1, "No")
            self.modules.SetItem(index, 2, item.get("description") or "Description unavailable.")
            self.modules.SetItem(index, 3, item["version"])
            self.set_module(index, retain is None or item["id"] in retain)
        if self.native:
            self.modules.Select(0)
            self.modules.Focus(0)

    def set_module(self, index, checked):
        if self.native_checkboxes:
            self.modules.CheckItem(index, checked)
        self.modules.SetItem(index, 1, "Yes" if checked else "No")

    def module_selected(self, index):
        if self.native_checkboxes:
            return self.modules.IsItemChecked(index)
        return self.modules.GetItemText(index, 1) == "Yes"

    def toggle_row(self, index):
        self.set_module(index, not self.module_selected(index))

    def module_changed(self, event):
        index = event.GetIndex()
        if 0 <= index < self.modules.GetItemCount():
            self.modules.SetItem(index, 1, "Yes" if self.modules.IsItemChecked(index) else "No")

    def toggle_module(self, event):
        index = event.GetIndex()
        if event.GetKeyCode() == wx.WXK_SPACE and index >= 0:
            self.toggle_row(index)
        else:
            event.Skip()

    def select_all(self, _):
        for index in range(len(self.native)):
            self.set_module(index, True)

    def change_source(self, _):
        if self.selected_catalog() is None:
            self.refresh_catalog(None)
            return
        self.show_modules(self.selected_modules())

    def report(self, message):
        self.log.AppendText(message.rstrip() + "\n")

    def notify(self, message, error=False):
        self.report(message)
        self.make_status.SetStatusText(message)
        self.dialog_open = True
        try:
            wx.MessageBox(message, "Cardputer ADV Firmware Builder",
                          wx.OK | (wx.ICON_ERROR if error else wx.ICON_INFORMATION), self)
        finally:
            self.dialog_open = False

    def on_close(self, event):
        if self.busy and event.CanVeto():
            event.Veto()
            self.notify("An operation is running. Wait for it to finish before closing.")
        else:
            event.Skip()

    def work(self, label, operation, completed):
        self.busy = True
        focus = wx.Window.FindFocus()
        self.log.SetFocus()
        self.GetMenuBar().EnableTop(0, False)
        for control in [*self.actions, self.source, self.modules, self.port, self.screen]:
            control.Disable()
        self.report(label)
        self.make_status.SetStatusText(label)

        def finish(result, error):
            self.busy = False
            self.GetMenuBar().EnableTop(0, True)
            for control in [*self.actions, self.source, self.modules, self.port]:
                control.Enable()
            self.update_screen_checkbox()
            if focus:
                focus.SetFocus()
            if error is not None:
                self.notify(str(error), error=True)
            else:
                completed(result)

        def worker():
            try:
                result = operation()
            except Exception as error:
                wx.CallAfter(finish, None, error)
            else:
                wx.CallAfter(finish, result, None)

        threading.Thread(target=worker, daemon=True).start()

    def refresh_catalog(self, _, startup=False):
        def ready(catalog):
            selection = self.selected_modules() if self.native else None
            self.catalog = catalog
            self.catalog_checked_at = monotonic()
            self.source.SetSelection(1 if self.local else 0)
            self.show_modules(selection)
            if startup and self.port_records:
                self.report(f"GitHub catalog {catalog['tag']} is ready.")
            else:
                self.notify(f"GitHub catalog {catalog['tag']} is ready.")
        self.work("Refreshing GitHub catalog.", refresh, ready)

    def update_builder(self, event):
        if self.busy or self.dialog_open:
            if event is None:
                wx.CallLater(1500, self.update_builder, None)
            return

        def ready(update):
            if not self:
                return
            if self.busy:
                wx.CallLater(1500, ready, update)
                return
            if update is None:
                if event is not None:
                    self.notify("No compatible builder update is available.")
                return
            if wx.MessageBox(
                f"Builder {update['version']} is available. Download the application files "
                "and use them the next time you open the builder?",
                "Builder update", wx.YES_NO | wx.NO_DEFAULT, self,
            ) == wx.YES:
                self.work("Downloading builder application update.", lambda: install_update(update),
                          lambda _: self.notify("Builder updated. Close and reopen it to use the new version."))

        if event is not None:
            self.work("Checking GitHub for builder updates.", check_update, ready)
        else:
            def background_check():
                try:
                    update = check_update()
                except (OSError, ValueError, KeyError, TypeError):
                    return
                if update is not None:
                    wx.CallAfter(ready, update)

            threading.Thread(target=background_check, daemon=True).start()

    def startup_device_check(self, port):
        if self.busy or self.dialog_open:
            wx.CallLater(1000, self.startup_device_check, port)
            return
        fallback = self.catalog or self.local
        recent = self.catalog if self.catalog_checked_at is not None and monotonic() - self.catalog_checked_at < 60 else None

        def perform():
            installed = read_identity(port)
            if installed is None:
                return None, device_update_summary(None, None)
            try:
                catalog = recent or refresh()
            except (OSError, ValueError, KeyError, TypeError):
                if fallback is None:
                    return None, "Could not check GitHub for firmware or app updates."
                return None, ("GitHub could not be checked. Compared with the available catalog.\n\n"
                              + device_update_summary(installed, fallback))
            return catalog, device_update_summary(installed, catalog)

        def ready(result):
            catalog, message = result
            if catalog is not None:
                retain = self.selected_modules() if self.native else None
                self.catalog = catalog
                self.catalog_checked_at = monotonic()
                if self.selected_catalog() is self.catalog:
                    self.show_modules(retain)
            self.notify(message)

        self.work("Checking installed Cardputer firmware and apps.", perform, ready)

    def refresh_ports(self, _=None):
        old = self.port.GetStringSelection()
        self.port_records = ports()
        self.port.Set([f"{port.device} — {port.description}" for port in self.port_records])
        if old and self.port.FindString(old) != wx.NOT_FOUND:
            self.port.SetStringSelection(old)
        elif len(self.port_records) == 1:
            self.port.SetSelection(0)

    def selected_port(self):
        index = self.port.GetSelection()
        if index == wx.NOT_FOUND:
            message = "Select the Cardputer USB port first."
            if not self.port_records:
                message = "No Cardputer USB port was found. Connect a data cable and refresh USB ports.\n\n" + RECOVERY
            self.notify(message, error=True)
            return None
        return self.port_records[index].device

    def command(self, args, *, cwd):
        if "--firmware" in args:
            phase = "Audio and keyboard dependency setup"
        elif "-m" in args and "platformio" in args:
            phase = f"PlatformIO toolchain setup and compilation with {args[args.index('-j') + 1]} jobs"
        else:
            phase = "Preparing the audio library"
        started = monotonic()
        compile_started = None
        wx.CallAfter(self.report, phase + ".")
        with subprocess.Popen(args, cwd=cwd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              creationflags=NO_CONSOLE) as process:
            for kind, line in output_events(process.stdout):
                if kind == "progress":
                    wx.CallAfter(self.make_status.SetStatusText, line)
                    continue
                if "Building in release mode" in line:
                    compile_started = monotonic()
                    wx.CallAfter(self.make_status.SetStatusText, "Compiling firmware")
                    wx.CallAfter(self.report, f"Dependency and toolchain checks took {compile_started - started:.1f} seconds. Compiling.")
                wx.CallAfter(self.report, line)
            if process.wait():
                raise RuntimeError(f"{phase} failed. See the activity log for details.")
        if compile_started is not None:
            wx.CallAfter(self.report, f"Compilation and linking finished in {monotonic() - compile_started:.1f} seconds.")
        else:
            wx.CallAfter(self.report, f"{phase} finished in {monotonic() - started:.1f} seconds.")

    def confirm_flash(self, output, port, mac, migration):
        complete = threading.Event()
        approved = False
        change = "Write the bootloader, single-app partition layout, and firmware." if migration else (
            "Write the application and keep the current partition layout.")
        message = (f"ESP32-S3 detected on {port}, MAC {mac}.\n\n{change}\n\n"
                   f"{RECOVERY}\n\nSaved recovery files: {output}\n\nContinue with flashing?")

        def ask():
            nonlocal approved
            try:
                self.report(message)
                with wx.Dialog(self, title="USB recovery and flash confirmation",
                               style=wx.DEFAULT_DIALOG_STYLE | wx.RESIZE_BORDER, size=(660, 450)) as dialog:
                    layout = wx.BoxSizer(wx.VERTICAL)
                    details = wx.TextCtrl(dialog, value=message,
                                          style=wx.TE_MULTILINE | wx.TE_READONLY,
                                          name="Device, recovery steps, and proposed flash")
                    layout.Add(details, 1, wx.EXPAND | wx.ALL, 10)
                    layout.Add(dialog.CreateButtonSizer(wx.OK | wx.CANCEL), 0, wx.EXPAND | wx.ALL, 10)
                    dialog.FindWindowById(wx.ID_OK).SetLabel("Flash firmware")
                    dialog.FindWindowById(wx.ID_CANCEL).SetDefault()
                    dialog.SetSizer(layout)
                    details.SetInsertionPoint(0)
                    details.SetFocus()
                    approved = dialog.ShowModal() == wx.ID_OK
            finally:
                complete.set()

        wx.CallAfter(ask)
        complete.wait()
        return approved

    def flash_output(self, port, output, **options):
        return flash(port, output, report=lambda line: wx.CallAfter(self.report, line),
                     confirm=lambda *device: self.confirm_flash(output, *device),
                     recommend_migration=self.recommend_migration, **options)

    def recommend_migration(self):
        finished = threading.Event()
        result = (False, None)

        def ask():
            nonlocal result
            try:
                self.report("The existing partition layout differs from the supported single-app layout.")
                result = self.migration_options(detected=True)
            finally:
                finished.set()

        wx.CallAfter(ask)
        finished.wait()
        return result

    def flash_saved(self, _):
        port = self.selected_port()
        if port is None:
            return
        with wx.FileDialog(self, "Open build.json from a saved build", defaultDir=str(STATE / "builds"),
                           wildcard="Builder manifest (build.json)|build.json", style=wx.FD_OPEN | wx.FD_FILE_MUST_EXIST) as picker:
            if picker.ShowModal() != wx.ID_OK:
                return
            output = Path(picker.GetPath()).parent
        self.work("Verifying saved build.", lambda: self.flash_output(port, output),
                  lambda mac: self.notify("Firmware flashed and launcher verified." if mac else "Flash cancelled."))

    def start_build(self, action, *, selected=None, port=None, backup=None):
        current = self.selected_catalog()
        if current is None:
            self.refresh_catalog(None)
            return
        if action != "build" and port is None:
            port = self.selected_port()
            if port is None:
                return
        if selected is None:
            selected = self.selected_modules()
        screen = current.get("supports_screen", False) and self.screen.GetValue()
        catalog = current if current is not self.local else None
        stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
        output = STATE / "builds" / (stamp + "-" + uuid4().hex[:8])

        def perform():
            if catalog is not None:
                wx.CallAfter(self.report, f"Checking the cached firmware source for {catalog['tag']}.")
            root = source_tree(catalog) if catalog is not None else ROOT
            build(root, selected, output, run=self.command, screen=screen)
            if port is not None:
                wx.CallAfter(self.report, f"USB recovery files: {output}")
                return self.flash_output(port, output, allow_migration=action == "migrate", backup=backup)
            return True

        def done(result):
            if not result:
                description = "Flash cancelled. The build is ready."
            else:
                description = "Build completed." if action == "build" else "Firmware flashed and launcher verified."
            self.notify(f"{description}\n\nBuild files: {output}")
        self.work("Building firmware.", perform, done)

    def update_firmware(self, _):
        catalog = self.selected_catalog()
        if catalog is None:
            self.refresh_catalog(None)
            return
        port = self.selected_port()
        if port is None:
            return

        def choose(installed):
            screen = catalog.get("supports_screen", False) and self.screen.GetValue()
            display = "LCD enabled" if screen else "LCD disabled"
            if installed is None:
                if wx.MessageBox(f"The device did not report a build version. Install with all current apps and {display}?",
                                 "Older or unidentified firmware", wx.YES_NO | wx.NO_DEFAULT, self) != wx.YES:
                    return
                selected = [item["id"] for item in self.native]
            else:
                selected = list(installed["modules"])
                available = {item["id"]: item["version"] for item in self.native}
                if any(module not in available for module in selected):
                    self.notify("The selected source is missing an installed app.", error=True)
                    return
                pairs = [(installed["launcher"], catalog["launcher"])]
                pairs.extend((current, available[name]) for name, current in installed["modules"].items())
                comparisons = [(version(current), version(offered)) for current, offered in pairs]
                if any(offered < current for current, offered in comparisons):
                    self.notify("The selected source would downgrade installed firmware.", error=True)
                    return
                if (all(offered == current for current, offered in comparisons)
                        and screen == installed.get("screen", False)):
                    self.notify("The launcher and selected apps are current.")
                    return
                offered = ", ".join(f"{name} {available[name]}" for name in selected) or "no optional apps"
                if wx.MessageBox(f"Install launcher {catalog['launcher']} with {offered} and {display}?",
                                 "Firmware update", wx.YES_NO | wx.NO_DEFAULT, self) != wx.YES:
                    return
            self.start_build("flash", selected=selected, port=port)
        self.work("Reading installed firmware version.", lambda: read_identity(port), choose)

    def migrate(self, _):
        if self.selected_catalog() is None:
            self.refresh_catalog(None)
            return
        port = self.selected_port()
        if port is None:
            return
        approved, backup = self.migration_options()
        if approved:
            self.start_build("migrate", port=port, backup=backup)

    def migration_options(self, *, detected=False):
        if detected and wx.MessageBox(
            "The Cardputer has a different partition layout. Install one large application partition "
            "using the selected firmware?",
            "Single application partition recommended", wx.YES_NO | wx.NO_DEFAULT, self,
        ) != wx.YES:
            return False, None
        choices = ["Proceed without a full flash backup", "Attempt and verify a full flash backup"]
        with wx.SingleChoiceDialog(
            self, "A full flash read can reboot some devices. Choose how to handle backup.",
            "Large partition backup", choices,
        ) as dialog:
            dialog.SetSelection(0)
            if dialog.ShowModal() != wx.ID_OK:
                return False, None
            wants_backup = dialog.GetSelection() == 1
        backup = None
        if wants_backup:
            with wx.FileDialog(self, "Save private flash backup", defaultFile="cardputer-original-flash.bin",
                               wildcard="Flash images (*.bin)|*.bin", style=wx.FD_SAVE) as picker:
                if picker.ShowModal() != wx.ID_OK:
                    return False, None
                backup = Path(picker.GetPath())
            if backup.exists():
                self.notify("Choose a new backup filename to preserve the existing file.", error=True)
                return False, None
        message = ("Install one 7.875 MiB application partition on this 8 MiB Cardputer ADV? "
                   "Remove the SD card. This may replace the bootloader and make old firmware or settings "
                   "inaccessible. The builder saves a USB recovery bundle for the new firmware. ")
        message += ("Your full backup may contain credentials. Keep it private." if wants_backup else
                    "Without a full backup, the recovery bundle cannot restore your old firmware or settings.")
        if wx.MessageBox(message, "Confirm large partition installation",
                         wx.YES_NO | wx.NO_DEFAULT | wx.ICON_WARNING, self) != wx.YES:
            return False, None
        return True, backup

    def update_sd(self, _):
        catalog = self.selected_catalog()
        if catalog is None:
            self.refresh_catalog(None)
            return
        local = ROOT if catalog is self.local else None
        with wx.DirDialog(self, "Choose the mounted Cardputer SD card",
                          style=wx.DD_DIR_MUST_EXIST) as picker:
            if picker.ShowModal() != wx.ID_OK:
                return
            card = Path(picker.GetPath())
        candidates = []
        labels = []
        try:
            for item in catalog["modules"]:
                if item["kind"] != "sd":
                    continue
                current = installed_version(card, item)
                if current is not None and version(current) >= version(item["version"]):
                    continue
                candidates.append(item)
                labels.append(f"{item['name']}: {current or 'not installed'} to {item['version']}")
        except (OSError, ValueError) as error:
            self.notify(str(error), error=True)
            return
        if not candidates:
            self.notify("SD packages are current.")
            return
        with wx.Dialog(self, title="SD packages", style=wx.DEFAULT_DIALOG_STYLE | wx.RESIZE_BORDER,
                       size=(560, 360)) as picker:
            layout = wx.BoxSizer(wx.VERTICAL)
            layout.Add(wx.StaticText(picker, label="Choose packages to install or update."), 0, wx.ALL, 8)
            packages = ScrolledPanel(picker)
            package_layout = wx.BoxSizer(wx.VERTICAL)
            packages.SetSizer(package_layout)
            checks = []
            for label in labels:
                checkbox = wx.CheckBox(packages, label=label)
                package_layout.Add(checkbox, 0, wx.ALL, 4)
                checks.append(checkbox)
            packages.SetupScrolling(scroll_x=False)
            layout.Add(packages, 1, wx.EXPAND | wx.LEFT | wx.RIGHT, 8)
            layout.Add(picker.CreateButtonSizer(wx.OK | wx.CANCEL), 0, wx.EXPAND | wx.ALL, 8)
            picker.SetSizer(layout)
            checks[0].SetFocus()
            if picker.ShowModal() != wx.ID_OK:
                return
            selected = [item for item, checkbox in zip(candidates, checks, strict=True) if checkbox.GetValue()]
        if selected:
            self.work("Updating SD packages.",
                      lambda: sum(install_sd(card, catalog, item, local_root=local) for item in selected),
                      lambda count: self.notify(f"Updated {count} SD packages."))


if __name__ == "__main__":
    application = wx.App()
    try:
        window = Builder()
        window.Show()
        application.MainLoop()
    except Exception as error:
        wx.MessageBox(str(error), "Cardputer ADV Firmware Builder", wx.OK | wx.ICON_ERROR)
        raise
