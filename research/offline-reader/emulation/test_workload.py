"""Compile a target workload and run it in the emulator, preserving all logs."""
from pathlib import Path
import importlib.util
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("emulator_runner", ROOT / "emulation/run_emulator.py")
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)

def main():
    environment = sys.argv[1] if len(sys.argv) > 1 else "emulator-workload"
    logs = ROOT / "test-results/emulation"
    logs.mkdir(parents=True, exist_ok=True)
    if not (ROOT / "generated/benchmark_tone.h").exists():
        subprocess.run([sys.executable, str(ROOT / "emulation/prepare_fixture.py")], check=True)
    result = subprocess.run([sys.executable, "-m", "platformio", "run", "-e", environment, "-j", "6"],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
    (logs / (environment + "-build.log")).write_text(result.stdout, encoding="utf-8")
    print("\n".join(line for line in result.stdout.splitlines() if any(x in line for x in ("error:", "RAM:", "Flash:", "SUCCESS", "FAILED"))), flush=True)
    if result.returncode:
        print("\n".join(result.stdout.splitlines()[-30:]))
        raise SystemExit(result.returncode)
    build = ROOT / ".pio/build" / environment
    output = runner.run(environment, build / "firmware.factory.bin", build / "firmware.elf",
        extra=("--exit-on", "BENCH_COMPLETE"), duration="40s", wall_timeout=90)
    if "BENCH_COMPLETE" not in output or "BENCH_FAIL" in output:
        raise SystemExit("Workload did not complete; inspect emulator log")
    pressure = runner.run(environment + "-reserve64k", build / "firmware.factory.bin", build / "firmware.elf",
        extra=("--exit-on", "BENCH_COMPLETE", "--inject-on", "BENCH_CONFIG", "--inject", "65536\n"),
        duration="40s", wall_timeout=90)
    if "BENCH_COMPLETE" not in pressure or "BENCH_FAIL" in pressure or "extra_reserve=65536" not in pressure:
        raise SystemExit("Memory-pressure workload did not complete; inspect emulator log")

if __name__ == "__main__": main()
