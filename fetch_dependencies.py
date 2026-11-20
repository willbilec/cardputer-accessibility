"""Fetch the pinned source checkouts used by the firmware and experiments."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess

RESEARCH = Path(__file__).resolve().parent / "research"
DEFAULT = (
    "BackgroundAudio", "Bluetooth-Keyboard-Mouse-Emulator", "M5Cardputer",
    "M5Unified", "M5GFX", "arduino-espeak-ng", "arduino-posix-fs",
    "arduino-audio-tools", "MP3PlayerForM5Cardputer",
)
FIRMWARE = {
    "BackgroundAudio": ("/src/", "!/src/libhelix-aac/", "/LICENSE", "/library.json", "/library.properties"),
    "Bluetooth-Keyboard-Mouse-Emulator": ("/src/", "/LICENSE"),
}


def git(*args, cwd, capture=False):
    return subprocess.run(
        ["git", "-c", "core.autocrlf=false", *args], cwd=cwd, check=True,
        text=True, stdout=subprocess.PIPE if capture else None,
    ).stdout


def fetch(record, paths=()):
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
        if paths:
            git("sparse-checkout", "set", "--no-cone", *paths, cwd=target)
            git("config", "remote.origin.promisor", "true", cwd=target)
            git("config", "remote.origin.partialclonefilter", "blob:none", cwd=target)
        flags = ["--filter=blob:none"] if paths else []
        git("fetch", "--depth=1", "--no-tags", *flags, "origin", commit, cwd=target)
        git("checkout", "--detach", "FETCH_HEAD", cwd=target)
    if not paths:
        git("submodule", "update", "--init", "--recursive", cwd=target)
    print(f"{name} is ready.", flush=True)


def main():
    records = json.loads((RESEARCH / "dependencies.json").read_text(encoding="utf-8"))
    by_name = {record["name"]: record for record in records}
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("names", nargs="*", metavar="DEPENDENCY")
    parser.add_argument("--all", action="store_true", help="Fetch all historical dependencies too")
    parser.add_argument("--firmware", action="store_true", help="Fetch only the builder's production inputs")
    parser.add_argument("--keyboard", action="store_true", help="Include Bluetooth keyboard sources with --firmware")
    args = parser.parse_args()
    if args.firmware:
        if args.names or args.all:
            parser.error("Select --firmware by itself")
        inputs = FIRMWARE if args.keyboard else {"BackgroundAudio": FIRMWARE["BackgroundAudio"]}
        print("Checking pinned " + ("audio and keyboard sources." if args.keyboard else "audio sources."), flush=True)
        with ThreadPoolExecutor(max_workers=len(inputs)) as pool:
            futures = [pool.submit(fetch, by_name[name], paths) for name, paths in inputs.items()]
            for future in futures:
                future.result()
        return
    names = by_name if args.all else args.names or DEFAULT
    for name in names:
        if name not in by_name:
            parser.error(f"unknown dependency {name}")
        fetch(by_name[name])


if __name__ == "__main__":
    main()
