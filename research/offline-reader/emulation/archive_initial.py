"""Preserve the failing experiment's exact image/source before the order test."""
from pathlib import Path
import hashlib
import shutil

ROOT = Path(__file__).resolve().parents[1]
build = ROOT / ".pio/build/emulator-workload"
out = ROOT / "test-results/emulation"
image = build / "firmware.factory.bin"
assert hashlib.sha256(image.read_bytes()).hexdigest() == "f271e07753b3ac97922473fbf5b6a2634d56a36a96028e8987ac3c81284df605"
shutil.copy2(image, out / "initial-workload.bin")
shutil.copy2(ROOT / "src/emulator_workload.cpp", out / "initial-workload.cpp.txt")
print("Failing workload image and source preserved")
