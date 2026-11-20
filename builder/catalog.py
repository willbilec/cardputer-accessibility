"""Release and cache handling for the public Cardputer catalog."""

import argparse
import io
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import tempfile
from urllib.request import Request, urlopen
from zipfile import ZipFile

from build import ROOT, STATE, digest, load_modules
from source_files import firmware_files

REPOSITORY = "willbilec/cardputer-accessibility"
GITHUB_API = f"https://api.github.com/repos/{REPOSITORY}"
RELEASE_BASE = f"https://github.com/{REPOSITORY}/releases/download"


def version(value):
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", value):
        raise ValueError(f"Unsupported version: {value}")
    return tuple(map(int, value.split(".")))


def device_update_summary(installed, catalog):
    if installed is None:
        return "The Cardputer did not report its firmware and app versions."
    current = version(installed["launcher"])
    offered = version(catalog["launcher"])
    native = {item["id"]: item for item in catalog["modules"] if item["kind"] == "native"}
    updates = []
    unavailable = []
    if offered > current:
        updates.append(f"Firmware {installed['launcher']} → {catalog['launcher']}")
    elif offered < current:
        unavailable.append("Firmware is newer than this catalog.")
    for name, installed_version in installed["modules"].items():
        item = native.get(name)
        if item is None:
            unavailable.append(f"{name} is missing from this catalog.")
        elif version(item["version"]) > version(installed_version):
            updates.append(f"{item['name']} {installed_version} → {item['version']}")
        elif version(item["version"]) < version(installed_version):
            unavailable.append(f"{item['name']} is newer than this catalog.")
    new_apps = [item["name"] for name, item in native.items() if name not in installed["modules"]
                and version(item.get("introduced", item["version"])) > current]
    if updates:
        heading = f"{len(updates)} {'update is' if len(updates) == 1 else 'updates are'} available."
    elif unavailable:
        heading = "No updates found for the versions this catalog can compare."
    else:
        heading = "The firmware and all installed apps are up to date."
    sections = [heading, *updates, *unavailable]
    if new_apps:
        sections.append("New apps available: " + ", ".join(new_apps) + ".")
    return "\n".join(sections)


def request(url):
    return urlopen(Request(url, headers={"User-Agent": "CardputerAccessibility"}), timeout=30)


def check_asset(asset):
    name, checksum, size = asset["file"], asset["sha256"], asset["size"]
    if not re.fullmatch(r"[A-Za-z0-9._-]+", name) or name in {".", ".."}:
        raise ValueError("Invalid release asset filename")
    if not re.fullmatch(r"[a-f0-9]{64}", checksum):
        raise ValueError("Invalid asset checksum")
    if type(size) is not int or not 0 < size <= 256 * 1024**2:
        raise ValueError("Invalid release asset size")


def check_catalog(catalog, tag):
    if catalog["schema"] != 1 or catalog["tag"] != tag:
        raise ValueError("Unsupported release catalog")
    if version(catalog["launcher"]) != version(tag.removeprefix("v")):
        raise ValueError("Launcher version differs from release tag")
    check_asset(catalog["source"])
    seen = set()
    for module in catalog["modules"]:
        name = module["id"]
        if name in seen or not re.fullmatch(r"[a-z][a-z0-9-]{0,39}", name):
            raise ValueError(f"Invalid or duplicate module ID: {name}")
        seen.add(name)
        version(module["version"])
        if module["kind"] == "sd":
            check_asset(module["asset"])
        elif module["kind"] != "native":
            raise ValueError(f"Invalid package type for {name}")


def cached_catalog(cache=STATE):
    path = cache / "catalog.json"
    if not path.is_file():
        return None
    catalog = json.loads(path.read_text(encoding="utf-8"))
    check_catalog(catalog, catalog["tag"])
    return catalog


def refresh(cache=STATE):
    with request(GITHUB_API + "/releases/latest") as response:
        release = json.load(response)
    tag = release["tag_name"]
    version(tag.removeprefix("v"))
    expected = f"{RELEASE_BASE}/{tag}/catalog.json"
    if expected not in {item["browser_download_url"] for item in release["assets"]}:
        raise ValueError(f"Release {tag} has no builder catalog. Use the local source or cached catalog.")
    with request(expected) as response:
        payload = response.read(256 * 1024 + 1)
    if len(payload) > 256 * 1024:
        raise ValueError("Catalog exceeds its size limit")
    catalog = json.loads(payload)
    check_catalog(catalog, tag)
    cache.mkdir(parents=True, exist_ok=True)
    path = cache / "catalog.json"
    temporary = path.with_suffix(".tmp")
    temporary.write_bytes(payload)
    temporary.replace(path)
    return catalog


def fetch_asset(catalog, asset, cache=STATE):
    tag = catalog["tag"]
    version(tag.removeprefix("v"))
    name, checksum = asset["file"], asset["sha256"]
    check_asset(asset)
    path = cache / "downloads" / checksum / name
    if path.is_file() and digest(path) == checksum:
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(".part")
    try:
        with request(f"{RELEASE_BASE}/{tag}/{name}") as response, temporary.open("wb") as output:
            remaining = asset["size"]
            while remaining:
                data = response.read(min(remaining, 1024 * 1024))
                if not data:
                    raise ValueError("Incomplete download")
                output.write(data)
                remaining -= len(data)
            if response.read(1):
                raise ValueError("Download larger than declared")
        if digest(temporary) != checksum:
            raise ValueError(f"Checksum mismatch for {name}")
        temporary.replace(path)
    finally:
        temporary.unlink(missing_ok=True)
    return path


def source_tree(catalog, cache=STATE):
    asset = catalog["source"]
    archive = fetch_asset(catalog, asset, cache)
    checksum = asset["sha256"]
    root = cache / "source" / checksum[:16]
    if root.is_dir():
        if (root / ".source-sha256").read_text(encoding="ascii") != checksum:
            raise ValueError("Cached source directory belongs to a different archive")
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=root.parent) as staging:
        tree = Path(staging) / "tree"
        tree.mkdir()
        with ZipFile(archive) as package:
            if sum(entry.file_size for entry in package.infolist()) > 512 * 1024**2:
                raise ValueError("Firmware source archive exceeds its extraction limit")
            for entry in package.infolist():
                relative = PurePosixPath(entry.filename)
                if relative.is_absolute() or ".." in relative.parts or any(
                        ":" in part or "\\" in part for part in relative.parts):
                    raise ValueError(f"Unsafe release path: {entry.filename}")
            package.extractall(tree)
        embedded = load_modules(tree)
        keys = ("id", "name", "version", "kind")
        actual = [{key: item[key] for key in keys} for item in embedded["modules"]]
        declared = [{key: item[key] for key in keys} for item in catalog["modules"]]
        if embedded["launcher"] != catalog["launcher"] or actual != declared:
            raise ValueError("Release source and catalog differ")
        (tree / ".source-sha256").write_text(checksum, encoding="ascii")
        tree.rename(root)
    return root


def package_folder(card, module):
    name = module["id"]
    if not re.fullmatch(r"[a-z][a-z0-9-]{0,39}", name):
        raise ValueError("Invalid SD module ID")
    card = card.resolve(strict=True)
    folder = card / "CardputerPackages" / name
    if folder.resolve() != folder:
        raise ValueError("SD package directory redirects to another location")
    return folder


def installed_version(card, module):
    folder = package_folder(card, module)
    versions = []
    if folder.is_dir():
        for candidate in folder.iterdir():
            if candidate.is_symlink() or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", candidate.name):
                continue
            if (candidate / ".complete").is_file():
                versions.append(candidate.name)
    return max(versions, key=version, default=None)


def install_sd(card, catalog, module, cache=STATE, *, local_root=None):
    previous = installed_version(card, module)
    current = module["version"]
    if previous is not None and version(previous) >= version(current):
        return False
    filename = module["filename"]
    if not re.fullmatch(r"[A-Za-z0-9_-]+\.[A-Za-z0-9]+", filename):
        raise ValueError("Invalid SD package filename")
    package = local_root / module["source"] if local_root else fetch_asset(catalog, module["asset"], cache)
    checksum = digest(package) if local_root else module["asset"]["sha256"]
    folder = package_folder(card, module)
    folder.mkdir(parents=True, exist_ok=True)
    target = folder / current
    if target.exists():
        raise ValueError(f"Existing unmanaged directory at {target}")
    with tempfile.TemporaryDirectory(prefix=".incoming-", dir=folder) as staging:
        tree = Path(staging) / "package"
        tree.mkdir()
        with package.open("rb") as source, (tree / filename).open("wb") as output:
            shutil.copyfileobj(source, output)
            output.flush()
            os.fsync(output.fileno())
        if digest(tree / filename) != checksum:
            raise ValueError("SD copy failed verification")
        (tree / ".complete").write_text(current, encoding="ascii")
        tree.rename(target)
    return True


def describe_asset(path):
    return {"file": path.name, "sha256": digest(path), "size": path.stat().st_size}


def prepare_release(tag, output):
    from program_update import write_update_assets

    manifest = load_modules(ROOT)
    if tag != "v" + manifest["launcher"]:
        raise ValueError("Tag must match the launcher version")
    output.mkdir(parents=True, exist_ok=True)
    archive = output / "firmware-source.zip"
    subprocess.check_call(["git", "archive", "--format=zip", "-o", str(archive.resolve()), tag,
                           "--", *firmware_files(ROOT, manifest)], cwd=ROOT)
    catalog = {"schema": 1, "tag": tag, "launcher": manifest["launcher"],
               "source": describe_asset(archive), "modules": []}
    for module in manifest["modules"]:
        entry = {key: module[key] for key in ("id", "name", "version", "kind")}
        entry["description"] = module.get("description", "")
        entry["introduced"] = module.get("introduced", module["version"])
        if module.get("requires"):
            entry["requires"] = module["requires"]
        if module["kind"] == "sd":
            asset = output / f"{module['id']}-{module['version']}.txt"
            shutil.copyfile(ROOT / module["source"], asset)
            entry.update(filename=module["filename"], asset=describe_asset(asset))
        catalog["modules"].append(entry)
    (output / "catalog.json").write_text(json.dumps(catalog, indent=2), encoding="utf-8")
    write_update_assets(ROOT, tag, output)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    prepare_release(args.tag, args.output)
