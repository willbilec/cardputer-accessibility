"""Create the Cardputer's low-memory SD bank from local PlayPalace effects."""
from argparse import ArgumentParser
from hashlib import sha256
from pathlib import Path
import struct
import subprocess

STEPS = (
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,
    66,73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,
    337,371,408,449,494,544,598,658,724,796,876,963,1060,1166,1282,
    1411,1552,1707,1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,
    4871,5358,5894,6484,7132,7845,8630,9493,10442,11487,12635,13899,
    15289,16818,18500,20350,22385,24623,27086,29794,32767,
)
ADJUSTMENTS = (-1, -1, -1, -1, 2, 4, 6, 8)


def encode_adpcm(pcm: bytes) -> bytes:
    """Encode mono samples using IMA ADPCM, low nibble first."""
    sample = index = 0
    output = bytearray((len(pcm) // 2 + 1) // 2)
    for i, (current,) in enumerate(struct.iter_unpack("<h", pcm)):
        step = STEPS[index]
        diff = current - sample
        code = 8 if diff < 0 else 0
        magnitude = abs(diff)
        delta = step >> 3
        for bit, fraction in ((4, step), (2, step >> 1), (1, step >> 2)):
            if magnitude >= fraction:
                code |= bit
                magnitude -= fraction
                delta += fraction
        sample = max(-32768, min(32767, sample + (-delta if code & 8 else delta)))
        index = max(0, min(88, index + ADJUSTMENTS[code & 7]))
        output[i // 2] |= code << (4 * (i % 2))
    return bytes(output)


def short_pcm(source: Path, name: str) -> bytes:
    seconds = "0.25" if name == "menuclick.ogg" else "4.01"
    result = subprocess.run(
        ["ffmpeg", "-nostdin", "-loglevel", "error", "-i", str(source),
         "-map", "0:a:0", "-t", seconds, "-ac", "1", "-ar", "16000",
         "-f", "s16le", "pipe:1"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True,
    )
    return result.stdout if 0 < len(result.stdout) <= 128000 else b""


def build(root: Path, destination: Path, directories: list[str]) -> None:
    effects = []
    sources = [root / name for name in directories]
    for directory in [root, *sources]:
        entries = directory.iterdir() if directory == root else directory.rglob("*")
        for source in sorted(entries):
            if not source.is_file() or source.suffix.lower() not in {".ogg", ".wav"}:
                continue
            stem = source.stem.casefold()
            if stem.endswith("mus") or any(word in stem for word in ("music", "ambience", "loop")):
                continue
            name = source.relative_to(root).as_posix()
            if name == "game_pig/turn.ogg" or directory.name != "game_pig":
                pcm = short_pcm(source, name)
                if pcm:
                    effects.append((sha256(name.encode()).digest()[:16], len(pcm) // 2, encode_adpcm(pcm)))
    effects.sort()
    if not 0 < len(effects) <= 1024 or len({key for key, _, _ in effects}) != len(effects):
        raise ValueError("Expected 1 to 1024 uniquely named short effects")
    offset = 8 + len(effects) * 24
    with destination.open("wb") as stream:
        stream.write(struct.pack("<4sI", b"PPF2", len(effects)))
        for key, samples, compressed in effects:
            stream.write(struct.pack("<16sII", key, offset, samples))
            offset += len(compressed)
        for _, _, compressed in effects:
            stream.write(compressed)
    print(f"Packed {len(effects)} effects into {offset} bytes")


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__)
    parser.add_argument("sounds", type=Path, help="PlayPalace desktop sounds directory")
    parser.add_argument("output", type=Path, help="Destination effects.ppfx file")
    parser.add_argument("--game-directory", action="append",
                        help="Include an additional sounds subdirectory")
    args = parser.parse_args()
    directories = ["game_skippo", "game_cards", "game_pig", "game_starrealms", "game_clank",
                   "game_spacebase", *(args.game_directory or [])]
    build(args.sounds, args.output, list(dict.fromkeys(directories)))
