"""Extract observed checkpoints, preserving failures as failures."""
from pathlib import Path
import json
import re

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "test-results/emulation"

def analyze(name):
    text = (OUT / (name + ".log")).read_text(encoding="utf-8")
    meta = json.loads((OUT / (name + ".json")).read_text(encoding="utf-8"))
    memories = [dict(stage=m[0], free=int(m[1]), minimum=int(m[2]), largest=int(m[3]),
                     calling_task_stack_free=int(m[4])) for m in re.findall(
        r"BENCH_MEM stage=(\S+) free=(\d+) minimum=(\d+) largest=(\d+) loop_stack=(\d+)", text)]
    first = [int(n) for n in re.findall(r"BENCH_FIRST_PCM emulated_ms=(\d+)", text)]
    oom = re.search(r"allocation of (\d+) bytes failed \(free_internal=(\d+) largest_internal_block=(\d+) free_psram=(\d+)\)", text)
    begin = re.search(r"BENCH_BEGIN extra_reserve=(\d+) psram=(\d+)", text)
    outcome = "completed" if "BENCH_COMPLETE" in text else "firmware_abort" if "Firmware abort:" in text else "incomplete"
    result = {"name": name, "outcome": outcome, "exit_code": meta["exit_code"],
        "image_sha256": meta["image_sha256"], "wall_seconds": meta["wall_seconds"],
        "extra_reserve": int(begin[1]) if begin else None,
        "psram_bytes": int(begin[2]) if begin else None,
        "first_pcm_emulated_ms": first,
        "completed_utterances": len(re.findall(r"BENCH_SPEECH_DONE generation=", text)),
        "wifi_connected": "BENCH_WIFI status=3" in text or "BENCH_WIFI_PRELOAD status=3" in text,
        "memory_checkpoints": memories,
        "speech_stack_minimum_free_bytes": [int(n) for n in re.findall(r"speech_stack=(\d+)", text)],
        "failure": {"request_bytes": int(oom[1]), "free_internal": int(oom[2]),
                    "largest_internal_block": int(oom[3]), "free_psram": int(oom[4])} if oom else None}
    return result

def main():
    names = ["emulator-workload", "emulator-workload-repeat", "emulator-workload-reserve64k", "emulator-all-first",
             "emulator-klatt", "emulator-klatt-reserve64k"]
    cases = [analyze(name) for name in names if (OUT / (name + ".log")).exists()]
    (OUT / "summary.json").write_text(json.dumps(cases, indent=2) + "\n", encoding="utf-8")
    for case in cases:
        print(json.dumps({k: v for k, v in case.items() if k != "memory_checkpoints"}))
    # Repeatability checks refer only to the exact initial-image runs.
    by_name = {case["name"]: case for case in cases}
    if "emulator-workload-repeat" in by_name:
        a, b = by_name["emulator-workload"], by_name["emulator-workload-repeat"]
        assert a["image_sha256"] == b["image_sha256"]
        assert a["failure"] == b["failure"]
        assert a["first_pcm_emulated_ms"] == b["first_pcm_emulated_ms"]
        print("Initial-image failure and first-PCM checkpoints reproduced exactly")

if __name__ == "__main__": main()
