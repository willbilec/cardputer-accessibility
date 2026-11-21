"""Fetch, checksum, and stage individual compatible builder update files."""

import hashlib
import json
from pathlib import Path
import re
import tempfile

from build import ROOT, STATE, digest, load_modules
from catalog import GITHUB_API, RELEASE_BASE, REPOSITORY, request, version

RUNTIME_ABI = 2
MAX_UPDATE_BYTES = 4 * 1024**2
PROGRAM_FILES = ("app.py", "build.py", "catalog.py", "flash.py", "progress.py",
                 "program_update.py", "source_files.py")


def check_update():
    installed = Path(__file__).with_name("program.json")
    current = (json.loads(installed.read_text(encoding="utf-8")) if installed.is_file()
               else {"version": load_modules(ROOT)["launcher"], "runtime_abi": RUNTIME_ABI})
    with request(GITHUB_API + "/releases/latest") as response:
        release = json.load(response)
    tag = release["tag_name"]
    latest = version(tag.removeprefix("v"))
    if latest <= version(current["version"]):
        return None
    expected = f"{RELEASE_BASE}/{tag}/builder-update.json"
    if expected not in {asset["browser_download_url"] for asset in release["assets"]}:
        return None
    with request(expected) as response:
        payload = response.read(16 * 1024 + 1)
    if len(payload) > 16 * 1024:
        raise ValueError("Program update metadata is too large.")
    update = json.loads(payload)
    if update.get("schema") != 2 or update.get("tag") != tag or version(update["version"]) != latest:
        raise ValueError("The program update does not match the GitHub release tag.")
    if update["runtime_abi"] != RUNTIME_ABI or current["runtime_abi"] != RUNTIME_ABI:
        return {"version": update["version"], "requires_full_runtime": True}
    _validate_manifest(update)
    return update


def _validate_manifest(update):
    files = update.get("files")
    if not isinstance(files, list) or not files:
        raise ValueError("The program update file list is invalid.")
    seen = set()
    total = 0
    for item in files:
        name = item.get("path") if isinstance(item, dict) else None
        size = item.get("size") if isinstance(item, dict) else None
        checksum = item.get("sha256") if isinstance(item, dict) else None
        is_module = isinstance(name, str) and bool(re.fullmatch(r"[a-z][a-z0-9_]*\.py", name))
        if not is_module or name in seen:
            raise ValueError("The program update contains an unexpected file.")
        if not isinstance(size, int) or isinstance(size, bool) or size < 0 \
                or not isinstance(checksum, str) or len(checksum) != 64:
            raise ValueError(f"The update metadata for {name} is invalid.")
        try:
            int(checksum, 16)
        except ValueError as error:
            raise ValueError(f"The checksum for {name} is invalid.") from error
        total += size
        if size > MAX_UPDATE_BYTES or total > MAX_UPDATE_BYTES:
            raise ValueError("The program update exceeds its size limit.")
        seen.add(name)
    if not set(PROGRAM_FILES).issubset(seen):
        raise ValueError("The program update is missing required files.")


def install_update(update, state=STATE):
    version(update["version"])
    if update["runtime_abi"] != RUNTIME_ABI:
        raise ValueError("Incompatible program update.")
    _validate_manifest(update)
    programs = state / "program"
    programs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=programs) as staging:
        candidate = Path(staging) / "app"
        candidate.mkdir()
        tag = update["tag"]
        if tag != "v" + update["version"]:
            raise ValueError("The program update tag does not match its version.")
        for item in update["files"]:
            name = item["path"]
            target = candidate / name
            target.parent.mkdir(parents=True, exist_ok=True)
            url = f"https://raw.githubusercontent.com/{REPOSITORY}/{tag}/builder/{name}"
            with request(url) as response:
                payload = response.read(item["size"] + 1)
            if len(payload) != item["size"] or hashlib.sha256(payload).hexdigest() != item["sha256"]:
                raise ValueError(f"Downloaded builder file failed checksum verification: {name}")
            target.write_bytes(payload)
        info = {"version": update["version"], "runtime_abi": RUNTIME_ABI}
        (candidate / "program.json").write_text(json.dumps(info), encoding="utf-8")
        if info != {"version": update["version"], "runtime_abi": RUNTIME_ABI}:
            raise ValueError("Program update version metadata differs.")
        for filename in (item["path"] for item in update["files"] if item["path"].endswith(".py")):
            compile((candidate / filename).read_bytes(), filename, "exec")
        destination = programs / info["version"]
        if not destination.exists():
            candidate.rename(destination)
        else:
            for filename in (*[item["path"] for item in update["files"]], "program.json"):
                if (destination / filename).read_bytes() != (candidate / filename).read_bytes():
                    raise ValueError("An installed copy of this program version differs.")
        pointer = Path(staging) / "program-update.json"
        pointer.write_text(json.dumps(info), encoding="utf-8")
        pointer.replace(state / "program-update.json")


def write_update_assets(root, tag, output):
    version_number = tag.removeprefix("v")
    version(version_number)
    files = []
    for name in PROGRAM_FILES:
        path = root / "builder" / name
        files.append({"path": name, "sha256": digest(path), "size": path.stat().st_size})
    metadata = {"schema": 2, "version": version_number, "runtime_abi": RUNTIME_ABI,
                "tag": tag, "files": files}
    (output / "builder-update.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
