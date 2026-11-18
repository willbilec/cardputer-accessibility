"""Browse or transfer files through the Cardputer's USB serial connection."""

import argparse
import os
from pathlib import Path, PurePosixPath
import tempfile
import time

import serial
from serial.tools import list_ports


def find_port():
    ports = [port.device for port in list_ports.comports()
             if (port.vid, port.pid) == (0x303A, 0x1001)]
    if len(ports) != 1:
        raise ValueError("Connect one Cardputer, or supply its COM port with --port")
    return ports[0]


def remote_path(path):
    raw = ("/" + path.lstrip("/")).encode("utf-8")
    if len(raw) > 256:
        raise ValueError("The remote path is longer than 256 bytes")
    return raw.hex()


def exchange(port, command):
    port.write((command + "\n").encode("ascii"))
    return response_line(port)


def response_line(port):
    line = port.readline()
    if not line.endswith(b"\n"):
        raise ValueError("No complete response. Check the connection and SD sharing mode on the Cardputer")
    response = line.rstrip(b"\r\n").decode("ascii")
    if response.startswith("ERR "):
        raise ValueError(response[4:])
    return response


def require_ok(port, command):
    if exchange(port, command) != "OK":
        raise ValueError("Unexpected response from the Cardputer")


def directory_entries(port, path):
    require_ok(port, "LIST " + remote_path(path))
    while (line := exchange(port, "NEXT")) != "END":
        fields = line.split()
        if len(fields) == 2 and fields[0] == "DIR":
            yield bytes.fromhex(fields[1]).decode("utf-8"), True, 0
        elif len(fields) == 3 and fields[0] == "FILE":
            yield bytes.fromhex(fields[2]).decode("utf-8"), False, int(fields[1])
        else:
            raise ValueError("Unexpected directory response")


def list_directory(port, path):
    for name, folder, size in directory_entries(port, path):
        print(name + "/" if folder else f"{name}  ({size} bytes)")


def publish_download(temp, target):
    if os.name == "nt":
        temp.rename(target)
    else:
        os.link(temp, target)


def download(port, remote, destination):
    target = Path(destination or PurePosixPath(remote).name)
    if target.exists():
        raise ValueError(f"Destination already exists: {target}")
    response = exchange(port, "READ " + remote_path(remote))
    if not response.startswith("OK "):
        raise ValueError("Expected a file size")
    remaining = int(response[3:])
    temp = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False, prefix=".cardputer-") as out:
            temp = Path(out.name)
            while (line := exchange(port, "NEXT")) != "END":
                if not line.startswith("DATA "):
                    raise ValueError("Expected file data")
                data = bytes.fromhex(line[5:])
                remaining -= len(data)
                if not data or remaining < 0:
                    raise ValueError("Invalid download size")
                out.write(data)
        if remaining:
            raise ValueError("Incomplete download")
        publish_download(temp, target)
    finally:
        if temp is not None:
            temp.unlink(missing_ok=True)
    print(f"Saved {target}")


def upload(port, source, remote):
    local = Path(source)
    remote = remote or "/" + local.name
    with local.open("rb") as stream:
        require_ok(port, "WRITE " + remote_path(remote))
        while data := stream.read(256):
            require_ok(port, "DATA " + data.hex())
        require_ok(port, "COMMIT")
    print(f"Saved {remote} on the Cardputer")


def download_stream(port, remote, target, progress):
    if target.exists():
        raise ValueError(f"Destination already exists: {target}")
    temp = None
    try:
        with tempfile.NamedTemporaryFile(dir=target.parent, delete=False, prefix=".cardputer-") as out:
            temp = Path(out.name)
            response = exchange(port, "GET " + remote_path(remote))
            if not response.startswith("OK "):
                raise ValueError("Expected a file size")
            total = int(response[3:])
            if total < 0:
                raise ValueError("Invalid file size")
            received = 0
            progress(0, total)
            while received < total:
                count = min(16384, total - received)
                data = port.read(count)
                if len(data) != count:
                    raise OSError("Download stopped before the file was complete.")
                out.write(data)
                received += count
                progress(received, total)
        publish_download(temp, target)
    finally:
        if temp is not None:
            temp.unlink(missing_ok=True)


def upload_stream(port, source, remote, progress, usb=False):
    with source.open("rb") as stream:
        total = os.fstat(stream.fileno()).st_size
        require_ok(port, f"PUT {total} {remote_path(remote)}")
        progress(0, total)
        sent = 0
        while sent < total:
            data = stream.read(min(2048 if usb else 16384, total - sent))
            if not data:
                raise OSError("The source file shrank during upload.")
            port.write(data)
            sent += len(data)
            if usb and sent < total and response_line(port) != "OK":
                raise ValueError("The Cardputer did not acknowledge the upload block.")
            progress(sent, total)
        if response_line(port) != "OK":
            raise ValueError("The Cardputer did not confirm the upload.")


def main():
    parser = argparse.ArgumentParser(
        description="Open Browse SD card over USB on the Cardputer before using this tool."
    )
    parser.add_argument("--port", help="COM port, for example COM5. Defaults to auto-detection.")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("ls", help="List a folder").add_argument("path", nargs="?", default="/")
    get = commands.add_parser("get", help="Copy a file to this computer")
    get.add_argument("remote")
    get.add_argument("local", nargs="?")
    put = commands.add_parser("put", help="Copy a new file to the card")
    put.add_argument("local")
    put.add_argument("remote", nargs="?")
    commands.add_parser("mkdir", help="Create a folder").add_argument("path")
    commands.add_parser("sync-time", help="Set the Cardputer clock from this computer")
    args = parser.parse_args()

    try:
        with serial.Serial(args.port or find_port(), 115200, timeout=4, write_timeout=4) as port:
            port.reset_input_buffer()
            if exchange(port, "HELLO") != "CARDFILES 1":
                raise ValueError("The Cardputer is not in Browse SD card over USB")
            try:
                if args.command == "ls":
                    list_directory(port, args.path)
                elif args.command == "get":
                    download(port, args.remote, args.local)
                elif args.command == "put":
                    upload(port, args.local, args.remote)
                elif args.command == "mkdir":
                    require_ok(port, "MKDIR " + remote_path(args.path))
                elif args.command == "sync-time":
                    require_ok(port, f"TIME {int(time.time())}")
                    print("Cardputer clock set from this computer")
            except (OSError, ValueError, KeyboardInterrupt):
                try:
                    port.write(b"CANCEL\n")
                except OSError:
                    pass
                raise
    except (OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")


if __name__ == "__main__":
    main()
