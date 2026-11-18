"""Run a pinned ESP32-S3 emulator under WSL; never writes a physical device."""
from pathlib import Path
import hashlib
import json
import subprocess
import tarfile
import time
import urllib.request
import sys
if hasattr(sys.stdout, "reconfigure"): sys.stdout.reconfigure(encoding="utf-8", errors="replace")

ROOT = Path(__file__).resolve().parents[1]
VERSION = "0.51.0"
ARCHIVE = f"esp-emu-{VERSION}-x86_64-unknown-linux-gnu.tar.gz"
URL = f"https://github.com/espressif/esp-emulator/releases/download/v{VERSION}/{ARCHIVE}"
SHA = "2b9e157470edb8b03167996550292c6df8cc6716025ebad8927db950d7931417"

def mount(path):
    path = Path(path).resolve()
    return "/mnt/" + path.drive[0].lower() + path.as_posix()[2:]

def install():
    target = ROOT / ".tools" / ("esp-emu-" + VERSION)
    target.mkdir(parents=True, exist_ok=True)
    archive = target / ARCHIVE
    if not archive.exists():
        urllib.request.urlretrieve(URL, archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != SHA:
        raise RuntimeError("Emulator release archive checksum mismatch")
    executable = next(target.rglob("esp-emu"), None)
    if executable is None:
        with tarfile.open(archive) as tar:
            tar.extractall(target, filter="data")
        executable = next(target.rglob("esp-emu"))
    return executable

def run(name, image, elf, extra=(), duration="10s", wall_timeout=60):
    image_sha256 = hashlib.sha256(Path(image).read_bytes()).hexdigest()
    emulator = install()
    results = ROOT / "test-results/emulation"
    results.mkdir(parents=True, exist_ok=True)
    command = ["wsl", "--exec", "timeout", "--kill-after=5s", str(wall_timeout)+"s", mount(emulator), "--chip", "esp32s3", "--psram-size", "0",
        "--firmware", mount(image), "--elf", mount(elf), "--timeout", duration,
        "--console", "usb-jtag", *extra]
    started = time.monotonic()
    timed_out = False
    try:
        p = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            timeout=wall_timeout+10)
        output = p.stdout.decode("utf-8", "replace")
        code = p.returncode
        timed_out = code in (124, 137)
    except subprocess.TimeoutExpired as exc:
        output = (exc.stdout or b"").decode("utf-8", "replace")
        code = None
        timed_out = True
    (results / (name + ".log")).write_text(output, encoding="utf-8")
    metadata = {"command": command, "emulator_version": VERSION, "release_archive_sha256": SHA,
        "image_sha256": image_sha256,
        "wall_seconds": round(time.monotonic() - started, 3), "exit_code": code,
        "wall_timeout": timed_out, "log": name + ".log"}
    (results / (name + ".json")).write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metadata, indent=2))
    print("\n".join(output.splitlines()[-45:]))
    return output

if __name__ == "__main__":
    run("launcher-stock", ROOT / "dist/initial-test-install.bin",
        ROOT / ".pio/build/offline-test-launcher/firmware.elf")
