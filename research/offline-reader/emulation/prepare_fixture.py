"""Generate an original 3-second stereo MP3 fixture, embedded in flash for tests."""
from pathlib import Path
import hashlib
import json
import subprocess

ROOT = Path(__file__).resolve().parents[1]

def main():
    output = ROOT / "generated"
    output.mkdir(exist_ok=True)
    fixture = output / "benchmark-tone.mp3"
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
        "sine=frequency=440:sample_rate=44100:duration=3", "-ac", "2", "-c:a", "libmp3lame",
        "-b:a", "128k", "-write_xing", "0", "-id3v2_version", "0", str(fixture)], check=True)
    data = fixture.read_bytes()
    body = ",\n".join(",".join(str(b) for b in data[i:i+32]) for i in range(0, len(data), 32))
    (output / "benchmark_tone.h").write_text(
        "#pragma once\n#include <pgmspace.h>\nstatic const unsigned char benchmarkTone[] PROGMEM = {\n" + body + "\n};\n", encoding="utf-8")
    metadata = {"source": "Generated 440 Hz sine, stereo 44.1 kHz, 128 kbps CBR MP3, 3 seconds",
        "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    (output / "benchmark-tone.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(metadata))

if __name__ == "__main__": main()
