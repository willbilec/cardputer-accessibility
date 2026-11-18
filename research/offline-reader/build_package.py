"""Build both images sequentially, verify their layout, and stage an SD package."""
from pathlib import Path
import hashlib
import json
import shutil
import struct
import subprocess
import sys
import argparse
from programs import PROGRAMS

ROOT = Path(__file__).resolve().parent
CORE = ROOT.parent / "Launcher/.pio/core"

def entries(data):
    result = {}
    for offset in range(0, len(data), 32):
        chunk = data[offset:offset + 32]
        if len(chunk) != 32 or chunk[:2] != b"\xaa\x50": continue
        _, kind, subtype, start, size, label, flags = struct.unpack("<HBBII16sI", chunk)
        result[label.rstrip(b"\0").decode()] = (kind, subtype, start, size)
    return result

def main():
    logs = ROOT / "test-results"
    logs.mkdir(exist_ok=True)
    parser = argparse.ArgumentParser()
    parser.add_argument("--package-only", action="store_true")
    parser.add_argument("--headless", action="store_true")
    args = parser.parse_args()
    package_only = args.package_only
    applications = PROGRAMS if args.headless else (("accessible-media-player","media-player","Media player"),)
    environments = tuple(app[0] for app in applications)+(("headless-klatt-launcher",) if args.headless else ("offline-test-launcher",))
    inputs = list((ROOT / "src").glob("*")) + list((ROOT / "include").glob("*"))
    inputs += [ROOT / "prepare.py", ROOT / "prepare_hid.py", ROOT / "programs.py", ROOT / "platformio.ini", ROOT / "partitions.csv",
               ROOT.parent / "speech-prototype/src/speech_service.cpp",
               ROOT.parent / "speech-prototype/src/speech_service.h",
               ROOT.parent / "MP3PlayerForM5Cardputer/MP3PlayerM5Cardputer.ino"]
    for environment in environments:
        if package_only:
            elf = ROOT / ".pio/build" / environment / "firmware.elf"
            if not elf.exists() or any(p.stat().st_mtime > elf.stat().st_mtime for p in inputs):
                raise SystemExit("Source changed or image missing; run a full package build first")
            continue
        result = subprocess.run([sys.executable, "-m", "platformio", "run", "-e", environment, "-j", "6"],
            cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
        (logs / (environment + "-build.log")).write_text(result.stdout, encoding="utf-8")
        for line in result.stdout.splitlines():
            if any(key in line for key in ("error:", "RAM:", "Flash:", "SUCCESS", "FAILED")): print(line, flush=True)
        if result.returncode:
            print("\n".join(result.stdout.splitlines()[-30:]))
            raise SystemExit(result.returncode)
    player = ROOT / ".pio/build" / environments[0]
    launcher = ROOT / ".pio/build" / environments[-1]
    table = (launcher / "partitions.bin").read_bytes()
    assert table == (player / "partitions.bin").read_bytes(), "Different application layouts"
    parsed = entries(table)
    assert parsed["reader"] == (0, 0, 0x10000, 0x280000)
    assert parsed["player"] == (0, 0x10, 0x290000, 0x560000)
    assert parsed["coredump"][2] + parsed["coredump"][3] == 0x800000
    assert (launcher / "firmware.bin").stat().st_size <= parsed["reader"][3]
    assert (player / "firmware.bin").stat().st_size <= parsed["player"][3]
    dist = ROOT / ("dist-headless" if args.headless else "dist")
    sd = dist / "sd-card/accessible"
    sd.mkdir(parents=True, exist_ok=True)
    app_files=[]
    for environment,program_id,label in applications:
        build=ROOT/".pio/build"/environment
        assert (build/"partitions.bin").read_bytes()==table,environment+": layout differs"
        assert (build/"firmware.bin").stat().st_size<=parsed["player"][3],environment+": oversized"
        output=sd/(program_id+".bin");shutil.copy2(build/"firmware.bin",output);app_files.append(output)
    shutil.copy2(launcher / "firmware.bin", dist / "offline-test-launcher.bin")
    shutil.copy2(launcher / "partitions.bin", dist / "partitions.bin")
    shutil.copy2(ROOT / "README.md", dist / "README.md")
    shutil.copy2(ROOT / "RESULTS.md", dist / "RESULTS.md")
    shutil.copy2(ROOT / "HEADLESS_RESULTS.md", dist / "HEADLESS_RESULTS.md")
    shutil.copy2(ROOT / "ADVANCED_TESTS.md", dist / "ADVANCED_TESTS.md")
    shutil.copy2(ROOT / "AUDIO_RESEARCH.md", dist / "AUDIO_RESEARCH.md")
    shutil.copy2(ROOT / "VELXIO_RESULTS.md", dist / "VELXIO_RESULTS.md")
    (dist / "emulation").mkdir(exist_ok=True)
    shutil.copy2(ROOT / "emulation/RESULTS.md", dist / "emulation/RESULTS.md")
    if args.headless:
        shutil.copy2(ROOT.parent / "moonshine/LICENSE", dist / "MOONSHINE-LICENSE.txt")
        shutil.copy2(ROOT.parent / "moonshine/micro/LICENSE", dist / "KLATT-CORE-LICENSE.txt")
        shutil.copy2(ROOT.parent / "M5Unified/LICENSE", dist / "M5STACK-LICENSE.txt")
        shutil.copy2(ROOT / "licenses/CMUDICT-LICENSE.txt", dist / "CMUDICT-LICENSE.txt")
        shutil.copy2(ROOT / "test-results/klatt-sample.wav", dist / "speech-preview.wav")
        shutil.copy2(ROOT.parent / "Bluetooth-Keyboard-Mouse-Emulator/LICENSE", dist / "KEYBOARD-MOUSE-LICENSE.txt")
        shutil.copy2(ROOT / "HID_AND_LEARNING.md", dist / "HID_AND_LEARNING.md")
    shutil.copy2(ROOT.parent / "MP3PlayerForM5Cardputer/LICENSE", dist / "PLAYER-LICENSE.txt")
    tool_python = CORE / "penv/Scripts/python.exe"
    if not tool_python.exists(): tool_python = Path(sys.executable)
    merged = dist / "initial-test-install.bin"
    subprocess.run([str(tool_python), str(CORE / "packages/tool-esptoolpy/esptool.py"),
        "--chip", "esp32s3", "merge-bin", "--output", str(merged),
        "--flash-mode", "dio", "--flash-freq", "80m", "--flash-size", "8MB",
        "0x0", str(launcher / "bootloader.bin"), "0x8000", str(launcher / "partitions.bin"),
        "0x10000", str(launcher / "firmware.bin")], check=True)
    assert merged.read_bytes()[0x8000:0x8000 + len(table)] == table
    launcher_raw = (launcher / "firmware.bin").read_bytes()
    assert merged.read_bytes()[0x10000:0x10000 + len(launcher_raw)] == launcher_raw
    for app_file in app_files:
        assert hashlib.sha256(app_file.read_bytes()).digest() in launcher_raw, "Launcher bundle hash missing: "+app_file.name
    # Erased OTA selection chooses the factory reader. Do not seed ota_0 on first install.
    assert merged.read_bytes()[0xE000:0x10000] == b"\xff" * 0x2000
    manifest = {
        "purpose": "Pre-purchase software feasibility prototype; hardware operation unverified",
        "upstream_player_commit": "b7548630a222403424386f2d298043e360661d6a",
        "variant": "headless-klatt" if args.headless else "graphical-espeak",
        "speech_revision": "234f60faa0eb388b01cdf7e60aca232af37aefda" if args.headless else None,
        "upstream_keyboard_mouse_commit": "2c1ca7a2fcf820d92aa0a0f238ce0d53a3a9995c" if args.headless else None,
        "programs": [{"id":program_id,"name":label,"sd_path":"accessible/"+program_id+".bin"} for environment,program_id,label in applications],
        "layout": {name: list(values) for name, values in parsed.items()},
        "files": {},
    }
    for path in (*app_files, dist / "offline-test-launcher.bin", merged, dist / "partitions.bin"):
        raw = path.read_bytes()
        manifest["files"][str(path.relative_to(dist))] = {"bytes": len(raw), "sha256": hashlib.sha256(raw).hexdigest()}
    (dist / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    shutil.copy2(dist / "manifest.json", sd / "manifest.json")
    archive = shutil.make_archive(str(ROOT / ("headless-klatt-test-package" if args.headless else "offline-reader-test-package")), "zip", dist)
    print("Package:", archive)
    print(json.dumps(manifest["files"], indent=2))

if __name__ == "__main__": main()
