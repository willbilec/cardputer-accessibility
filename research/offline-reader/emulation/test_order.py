"""Test long-lived Wi-Fi/decoder allocations before speech initialization."""
import subprocess
import sys
from run_emulator import ROOT, run

logs = ROOT / "test-results/emulation"
result = subprocess.run([sys.executable, "-m", "platformio", "run", "-e", "emulator-workload", "-j", "6"],
    cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
(logs / "order-build.log").write_text(result.stdout, encoding="utf-8")
print("\n".join(line for line in result.stdout.splitlines() if any(x in line for x in ("error:", "RAM:", "Flash:", "SUCCESS", "FAILED"))), flush=True)
if result.returncode: raise SystemExit(result.returncode)
build = ROOT / ".pio/build/emulator-workload"
output = run("emulator-all-first", build / "firmware.factory.bin", build / "firmware.elf",
    extra=("--exit-on", "BENCH_COMPLETE", "--inject-on", "BENCH_CONFIG", "--inject", "0,all-first\n"),
    duration="40s", wall_timeout=90)
if "BENCH_ORDER all_first=1" not in output or "BENCH_COMPLETE" not in output or "BENCH_FAIL" in output:
    raise SystemExit("Allocation-order experiment did not complete; inspect the captured failure")
