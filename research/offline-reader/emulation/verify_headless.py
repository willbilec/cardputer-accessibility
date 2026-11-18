"""Check actual linked firmware for the graphics dependencies we removed."""
from pathlib import Path
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]
TOOL = ROOT.parent / "Launcher/.pio/core/packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm.exe"

def main():
    result = {}
    for env in ("headless-klatt-player", "headless-klatt-launcher", "accessible-hid", "key-learning"):
        elf = ROOT / ".pio/build" / env / "firmware.elf"
        symbols = subprocess.check_output([str(TOOL), "-C", str(elf)], text=True, encoding="utf-8", errors="replace")
        graphics = [line for line in symbols.splitlines() if any(marker in line for marker in
            ("lgfx::", "m5gfx::", "M5Unified::", "M5GFX::", "LGFX_Sprite", "ST7789", "fft_t::"))]
        assert not graphics, graphics[:10]
        result[env] = {"graphics_symbols": graphics, "binary_bytes": elf.with_suffix(".bin").stat().st_size,
            "klatt_present": "tts::StreamSynth::" in symbols,
            "espeak_present": "espeak_Synth" in symbols}
        assert result[env]["klatt_present"] and not result[env]["espeak_present"]
        if env=="key-learning":
            result[env]["hid_transport_symbols"]=[line for line in symbols.splitlines() if any(marker in line for marker in
                ("BLEDevice::", "USBHIDKeyboard::", "USBHIDMouse::", "initBluetooth()"))]
            assert not result[env]["hid_transport_symbols"]
    (ROOT / "test-results/headless-symbol-audit.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(result, indent=2))

if __name__ == "__main__": main()
