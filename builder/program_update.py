"""Download and stage a compatible program-only GitHub update."""

import json
from pathlib import Path
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

from build import ROOT, STATE, load_modules
from catalog import GITHUB_API, RELEASE_BASE, describe_asset, fetch_asset, request, version

RUNTIME_ABI = 2
PROGRAM_FILES = ("app.py", "build.py", "catalog.py", "flash.py", "progress.py",
                 "program_update.py", "source_files.py")
PROGRAM_ASSETS = tuple(
    "help/" + path.name for path in sorted(Path(__file__).with_name("help").iterdir()) if path.is_file()
)


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
    if update["tag"] != tag or version(update["version"]) != latest:
        raise ValueError("The program update does not match the GitHub release tag.")
    if update["runtime_abi"] != current["runtime_abi"]:
        raise ValueError(
            "This program release needs a newer runtime. Download the latest Windows ZIP from "
            "https://github.com/willbilec/cardputer-accessibility/releases/latest."
        )
    return update


def install_update(update, state=STATE):
    version(update["version"])
    if update["runtime_abi"] != RUNTIME_ABI:
        raise ValueError("Incompatible program update.")
    archive = fetch_asset(update, update["asset"], state)
    programs = state / "program"
    programs.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=programs) as staging:
        candidate = Path(staging) / "app"
        candidate.mkdir()
        with ZipFile(archive) as package:
            names = [entry.filename for entry in package.infolist()]
            if sorted(names) != sorted((*PROGRAM_FILES, *PROGRAM_ASSETS, "program.json")):
                raise ValueError("Unexpected files in the program update.")
            if sum(entry.file_size for entry in package.infolist()) > 4 * 1024**2:
                raise ValueError("Program update exceeds its extraction limit.")
            package.extractall(candidate)
        info = json.loads((candidate / "program.json").read_text(encoding="utf-8"))
        if info != {"version": update["version"], "runtime_abi": RUNTIME_ABI}:
            raise ValueError("Program update version metadata differs.")
        for filename in PROGRAM_FILES:
            compile((candidate / filename).read_bytes(), filename, "exec")
        destination = programs / info["version"]
        if not destination.exists():
            candidate.rename(destination)
        else:
            for filename in (*PROGRAM_FILES, *PROGRAM_ASSETS, "program.json"):
                if (destination / filename).read_bytes() != (candidate / filename).read_bytes():
                    raise ValueError("An installed copy of this program version differs.")
        pointer = Path(staging) / "program-update.json"
        pointer.write_text(json.dumps(info), encoding="utf-8")
        pointer.replace(state / "program-update.json")


def write_update_assets(root, tag, output):
    info = {"version": tag.removeprefix("v"), "runtime_abi": RUNTIME_ABI}
    archive = output / "builder-update.zip"
    with ZipFile(archive, "w", compression=ZIP_DEFLATED) as package:
        for name in (*PROGRAM_FILES, *PROGRAM_ASSETS):
            package.write(root / "builder" / name, name)
        package.writestr("program.json", json.dumps(info))
    metadata = dict(info, tag=tag, asset=describe_asset(archive))
    (output / "builder-update.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
