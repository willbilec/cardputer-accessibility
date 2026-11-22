"""Inspect, program, and verify a Cardputer ADV over USB."""

import hashlib
import json
import os
import re
import struct
import subprocess
import time

import serial
from serial.tools.list_ports import comports

from build import APP_CAPACITY, NO_CONSOLE, PYTHON, digest

FLASH_BYTES = 0x800000
PARTITION_ADDRESS = 0x8000
APP_ADDRESS = 0x10000
MIGRATION_FILES = {0: "bootloader.bin", PARTITION_ADDRESS: "partitions.bin",
                   0xE000: "boot_app0.bin", APP_ADDRESS: "firmware.bin"}


def ports():
    return [port for port in comports() if port.vid == 0x303A]


def usb_serial(port):
    return (port.serial_number or "").replace(":", "").casefold()


def same_usb_device(first, second):
    first_serial, second_serial = usb_serial(first), usb_serial(second)
    if first_serial and second_serial:
        # The launcher and ROM can use different IDs on the same USB device.
        if first_serial == second_serial:
            return True
    first_bus = (first.location or "").partition(":")[0]
    return bool(first_bus and first_bus == (second.location or "").partition(":")[0])


def read_identity(port):
    with serial.Serial(port, 115200, timeout=0.25) as link:
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            link.write(b"UF_ID\n")
            line = link.read_until(size=1024)
            if line.startswith(b"UF_ID ") and line.endswith(b"\n"):
                return json.loads(line[6:])
    return None


def esptool(port, *args, after="no-reset"):
    command = [str(PYTHON), "-X", "utf8", "-m", "esptool",
               "--chip", "esp32s3", "--port", port,
               "--before", "no-reset", "--after", after,
               "--connect-attempts", "2", *map(str, args)]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            encoding="utf-8", errors="replace", timeout=300, creationflags=NO_CONSOLE)
    if result.returncode:
        raise RuntimeError(result.stdout.strip())
    return result.stdout


def identify(port):
    output = esptool(port, "flash-id")
    mac = re.search(r"\bMAC:\s*((?:[\da-f]{2}:){5}[\da-f]{2})", output, re.I)
    if not mac or not re.search(r"Detected flash size:\s*8\s*MB", output, re.I):
        raise ValueError("Expected an ESP32-S3 with 8 MiB flash")
    return mac[1].lower()


def find_download_port(selected):
    original = next((port for port in ports() if port.device == selected), None)
    if original is None:
        raise ValueError("Selected USB device disappeared. Refresh USB ports and try again.")
    # The launcher and ROM use different interfaces on Windows. Probe the
    # launcher before esptool, whose serial connection can disturb the handoff.
    serial_id = usb_serial(original)
    rom_port = usb_interface(original) == "0"
    if rom_port or ":" in (original.serial_number or ""):
        try:
            mac = identify(selected)
        except RuntimeError:
            pass
        else:
            if re.fullmatch(r"[a-f0-9]{12}", serial_id) and mac.replace(":", "") != serial_id:
                raise ValueError("Download mode MAC differs from the selected device.")
            return selected, mac
    try:
        with serial.Serial(selected, 1200, timeout=0.25):
            time.sleep(0.25)
    except OSError:
        pass  # The application port may disconnect during the 1200-baud touch.
    deadline = time.monotonic() + 15
    time.sleep(0.3)
    while time.monotonic() < deadline:
        for port in ports():
            if not same_usb_device(original, port) and not (
                    port.device == selected and not serial_id and not usb_serial(port)):
                continue
            if usb_interface(original) == "1" and port.location == original.location and (
                    time.monotonic() < deadline - 5):
                continue
            try:
                mac = identify(port.device)
            except RuntimeError:
                continue
            candidate_id = usb_serial(port)
            if re.fullmatch(r"[a-f0-9]{12}", candidate_id) and mac.replace(":", "") != candidate_id:
                raise ValueError("Download mode MAC differs from the selected device.")
            return port.device, mac
        time.sleep(0.3)
    raise ValueError("Download port could not be matched. Enter download mode with G0 and select its port.")


def usb_interface(port):
    return (port.location or "").partition(":")[2].rpartition(".")[2]


def check_layout(table):
    if len(table) != 0xC00:
        raise ValueError("Partition table has an unexpected size")
    entries = []
    for start in range(0, len(table) - 31, 32):
        magic, kind, subtype, offset, size, name, flags = struct.unpack_from(
            "<HBBII16sI", table, start)
        if magic != 0x50AA:
            break
        entries.append((kind, subtype, offset, size, name.split(b"\0")[0], flags))
    expected = [
        (1, 2, 0x9000, 0x5000, b"nvs", 0),
        (1, 0, 0xE000, 0x2000, b"otadata", 0),
        (0, 0, APP_ADDRESS, APP_CAPACITY, b"reader", 0),
        (1, 3, 0x7F0000, 0x10000, b"coredump", 0),
    ]
    if entries != expected:
        raise ValueError("Build does not contain the supported 8 MiB partition layout")
    marker = len(expected) * 32
    checksum = hashlib.md5(table[:marker], usedforsecurity=False).digest()
    if (table[marker:marker + 16] != b"\xEB\xEB" + b"\xFF" * 14
            or table[marker + 16:marker + 32] != checksum
            or any(byte != 0xFF for byte in table[marker + 32:])):
        raise ValueError("Partition table checksum or padding is invalid")


def verify_build(directory):
    manifest = json.loads((directory / "build.json").read_text(encoding="utf-8"))
    for name in MIGRATION_FILES.values():
        if digest(directory / name) != manifest["sha256"][name]:
            raise ValueError(f"Build checksum mismatch: {name}")
    table = (directory / "partitions.bin").read_bytes()
    check_layout(table)
    app = directory / "firmware.bin"
    if not 0 < app.stat().st_size <= APP_CAPACITY:
        raise ValueError("Application exceeds the partition size")
    if not 0 < (directory / "bootloader.bin").stat().st_size <= PARTITION_ADDRESS:
        raise ValueError("Bootloader exceeds its flash region")
    if (directory / "boot_app0.bin").stat().st_size != APP_ADDRESS - 0xE000:
        raise ValueError("Boot selection data has an unexpected size")
    return manifest


def help_response(link):
    line = link.read_until(size=1024)
    if not line.endswith(b"\n"):
        raise ValueError("The Cardputer did not reply over USB")
    return line.rstrip(b"\r\n").decode("ascii")


def help_command(link, command):
    link.write((command + "\n").encode("ascii"))
    return help_response(link)


def expect_help_ok(response):
    if response != "OK":
        raise ValueError(response.removeprefix("ERR "))


def compare_help(link, remote, contents):
    response = help_command(link, "READ " + remote)
    if response == "ERR File unavailable":
        return None
    if not response.startswith("OK "):
        raise ValueError(response.removeprefix("ERR "))
    if int(response[3:]) != len(contents):
        expect_help_ok(help_command(link, "CANCEL"))
        return False
    offset = 0
    while (response := help_command(link, "NEXT")) != "END":
        if not response.startswith("DATA "):
            raise ValueError(response.removeprefix("ERR "))
        data = bytes.fromhex(response[5:])
        if not data or data != contents[offset:offset + len(data)]:
            expect_help_ok(help_command(link, "CANCEL"))
            return False
        offset += len(data)
    return offset == len(contents)


def copy_help_to_card(port, directory, manifest):
    documents = []
    for name in manifest["help_documents"]:
        if not re.fullmatch(r"[A-Za-z0-9_-]{1,70}\.txt", name):
            raise ValueError("Invalid build help filename")
        relative = f"sdcard/Help/{name}"
        data = (directory / relative).read_bytes()
        if not 0 < len(data) <= 65536 or hashlib.sha256(data).hexdigest() != manifest["sha256"].get(relative):
            raise ValueError(f"Invalid help file or checksum mismatch: {name}")
        documents.append((name, data))
    with serial.Serial(port, 115200, timeout=3, write_timeout=3) as link:
        link.reset_input_buffer()
        response = help_command(link, "UF_HELP")
        if response != "CARDFILES HELP 1":
            raise ValueError(response.removeprefix("ERR "))
        try:
            expect_help_ok(help_command(link, "MKDIR " + b"/Help".hex()))
            for name, data in documents:
                remote = ("/Help/" + name).encode("ascii").hex()
                present = compare_help(link, remote, data)
                if present:
                    continue
                operation = "PUT" if present is None else "REPLACE"
                expect_help_ok(help_command(link, f"{operation} {len(data)} {remote}"))
                for offset in range(0, len(data), 2048):
                    link.write(data[offset:offset + 2048])
                    expect_help_ok(help_response(link))
                if compare_help(link, remote, data) is not True:
                    raise ValueError(f"Readback failed for {name}")
            expect_help_ok(help_command(link, "DONE"))
        except (OSError, ValueError):
            try:
                help_command(link, "DONE")
            except (OSError, ValueError):
                pass
            raise
    return len(documents)


def flash(selected, directory, *, allow_migration=False, backup=None, report=print, confirm=None,
          recommend_migration=None, help_result=None):
    manifest = verify_build(directory)
    app = directory / "firmware.bin"
    report("Entering USB download mode.")
    port, mac = find_download_port(selected)
    download = next((item for item in comports() if item.device == port), None)
    report(f"Connected to ESP32-S3 {mac} on {port}. Checking partition table.")
    migration = False
    try:
        esptool(port, "verify-flash", PARTITION_ADDRESS, directory / "partitions.bin")
    except RuntimeError as error:
        if "Verification failed (digest mismatch)" not in str(error):
            raise
        migration = True
    if migration and not allow_migration:
        if recommend_migration is None:
            esptool(port, "flash-id", after="watchdog-reset")
            raise ValueError("Partition table differs. Use Install large partition to migrate.")
        approved, backup = recommend_migration()
        if not approved:
            esptool(port, "flash-id", after="watchdog-reset")
            return None
    if confirm is not None and not confirm(port, mac, migration):
        esptool(port, "flash-id", after="watchdog-reset")
        return None
    if migration and backup is not None:
        report("Reading and verifying a private backup of the original flash.")
        backup = backup.resolve()
        os.close(os.open(backup, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600))
        try:
            esptool(port, "read-flash", 0, FLASH_BYTES, backup)
            if backup.stat().st_size != FLASH_BYTES:
                raise ValueError("Flash backup is incomplete")
            esptool(port, "verify-flash", 0, backup)
        except Exception:
            backup.unlink(missing_ok=True)
            raise
    if identify(port) != mac:
        raise ValueError("Connected device changed")
    paths = MIGRATION_FILES if migration else {APP_ADDRESS: app.name}
    pairs = [argument for offset, name in paths.items() for argument in (offset, directory / name)]
    report("Writing large partition layout and firmware." if migration else "Writing application.")
    esptool(port, "write-flash", "--flash-size", "8MB", *pairs)
    report("Verifying written flash.")
    esptool(port, "verify-flash", "--flash-size", "8MB", *pairs)
    esptool(port, "flash-id", after="watchdog-reset")
    report("Waiting for the launcher to report its version.")
    expected = {key: manifest[key] for key in ("launcher", "modules")}
    expected.update({key: manifest[key] for key in ("screen", "include_help") if key in manifest})
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        for candidate in ports():
            if download and not same_usb_device(download, candidate):
                if usb_serial(download) or candidate.device not in {port, selected}:
                    continue
            elif download is None and candidate.device not in {port, selected}:
                continue
            try:
                verified = read_identity(candidate.device) == expected
            except (OSError, ValueError):
                continue
            if verified:
                if manifest.get("help_documents"):
                    report("Copying help files to the Cardputer microSD over USB.")
                    try:
                        count = copy_help_to_card(candidate.device, directory, manifest)
                        message = f"Verified {count} help files in /Help on the microSD card."
                    except (OSError, ValueError) as error:
                        message = (f"Help copy could not finish. {error}. "
                                   f"The help files are saved in {directory / 'sdcard/Help'}.")
                    report(message)
                    if help_result is not None:
                        help_result(message)
                return mac
        time.sleep(0.25)
    raise RuntimeError("Flash verified, but the launcher did not report the expected version after reboot.")
