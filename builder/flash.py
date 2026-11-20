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
    try:
        return selected, identify(selected)
    except RuntimeError:
        pass
    before = {port.device: port for port in comports()}
    original = before.get(selected)
    if original is None:
        raise ValueError("Selected USB device disappeared")
    try:
        with serial.Serial(selected, 1200, timeout=0.25):
            time.sleep(0.25)
    except OSError:
        pass  # The application port may disconnect during the 1200-baud touch.
    deadline = time.monotonic() + 12
    while time.monotonic() < deadline:
        for port in ports():
            same_location = original.location and original.location == port.location
            same_serial = original.serial_number and original.serial_number == port.serial_number
            if port.device != selected and not same_location and not same_serial:
                continue
            try:
                return port.device, identify(port.device)
            except RuntimeError:
                continue
        time.sleep(0.3)
    raise ValueError("Download port could not be matched. Enter download mode with G0 and select its port.")


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


def flash(selected, directory, *, allow_migration=False, backup=None, report=print, confirm=None,
          recommend_migration=None):
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
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        for candidate in ports():
            if candidate.device not in {port, selected} and not (
                download and ((download.location and candidate.location == download.location)
                              or (download.serial_number and candidate.serial_number == download.serial_number))
            ):
                continue
            try:
                if read_identity(candidate.device) == expected:
                    return mac
            except (OSError, ValueError):
                continue
        time.sleep(0.25)
    raise RuntimeError("Flash verified, but the launcher did not report the expected version after reboot.")
