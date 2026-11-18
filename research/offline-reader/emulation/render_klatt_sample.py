from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
mount = "/mnt/" + ROOT.drive[0].lower() + ROOT.as_posix()[2:]
core = "../moonshine/micro"
command = f"cd '{mount}' && g++ -std=c++17 -O2 -Wall -Wextra -I{core}/klatt-tts/include -I{core}/klatt-tts/include/tts -I{core}/klatt-tts/src -I{core}/g2p/include -I{core}/g2p/include/g2p -I{core}/g2p/src test/test_klatt_host.cpp {core}/klatt-tts/src/*.cc {core}/g2p/src/*.cc -o /tmp/cardputer-klatt-host && /tmp/cardputer-klatt-host '{mount}/test-results/klatt-sample.wav'"
result = subprocess.run(["wsl", "--exec", "sh", "-lc", command], stdout=subprocess.PIPE,
    stderr=subprocess.STDOUT, text=True, encoding="utf-8", errors="replace")
(ROOT / "test-results/klatt-host.txt").write_text(result.stdout, encoding="utf-8")
print(result.stdout)
raise SystemExit(result.returncode)
