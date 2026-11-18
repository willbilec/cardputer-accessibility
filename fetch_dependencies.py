"""Fetch the pinned source checkouts used by the firmware and experiments."""

import argparse
import json
from pathlib import Path
import subprocess

RESEARCH = Path(__file__).resolve().parent / "research"
DEFAULT = (
    "BackgroundAudio", "Bluetooth-Keyboard-Mouse-Emulator", "M5Cardputer",
    "M5Unified", "M5GFX", "arduino-espeak-ng", "arduino-posix-fs",
    "arduino-audio-tools", "MP3PlayerForM5Cardputer",
)


def git(*args, cwd, capture=False):
    return subprocess.run(
        ["git", "-c", "core.autocrlf=false", *args], cwd=cwd, check=True,
        text=True, stdout=subprocess.PIPE if capture else None,
    ).stdout


def fetch(record):
    name, url, commit = (record[key] for key in ("name", "url", "commit"))
    target = RESEARCH / name
    if not target.exists():
        git("init", name, cwd=RESEARCH)
        git("remote", "add", "origin", url, cwd=target)
    elif not (target / ".git").exists():
        raise SystemExit(f"{target} exists and is not a dependency checkout.")

    if git("remote", "get-url", "origin", cwd=target, capture=True).strip() != url:
        raise SystemExit(f"Unexpected origin for {name}.")
    current = git("rev-parse", "--revs-only", "HEAD", cwd=target, capture=True).strip()
    if current and current != commit:
        raise SystemExit(f"{name} is at {current}. Expected {commit}.")
    if not current:
        git("fetch", "--depth=1", "origin", commit, cwd=target)
        git("checkout", "--detach", "FETCH_HEAD", cwd=target)
    git("submodule", "update", "--init", "--recursive", cwd=target)


def main():
    records = json.loads((RESEARCH / "dependencies.json").read_text(encoding="utf-8"))
    by_name = {record["name"]: record for record in records}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("names", nargs="*", metavar="DEPENDENCY")
    parser.add_argument("--all", action="store_true", help="Fetch all historical dependencies too")
    args = parser.parse_args()
    names = by_name if args.all else args.names or DEFAULT
    for name in names:
        if name not in by_name:
            parser.error(f"unknown dependency {name}")
        fetch(by_name[name])


if __name__ == "__main__":
    main()
