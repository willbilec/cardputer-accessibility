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

from build import ROOT, STATE, build, digest, load_modules, project_lock, save_build
from compile_firmware import PLATFORM_URL, SDK_ASSET, SDK_PACKAGE
from flash import MIGRATION_FILES, verify_build

REPOSITORY = "willbilec/cardputer-accessibility"
GITHUB_API = f"https://api.github.com/repos/{REPOSITORY}"
RELEASE_BASE = f"https://github.com/{REPOSITORY}/releases/download"
SOURCE_BASE = f"https://codeload.github.com/{REPOSITORY}/zip"
RAW_BASE = f"https://raw.githubusercontent.com/{REPOSITORY}"
RELEASE_METADATA = "<!-- cardputer-builder-release\n"
UNIVERSAL_FIRMWARE = "cardputer-adv-full-firmware.zip"
SHARED_SDK = {
    "file": SDK_ASSET, "release_tag": "v0.2.8", "size": 65860204,
    "sha256": "515fc3fbef45858999aeccdf841a9de756d0697c4ecbec904d97e8037801961a",
}


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
    if "path" in asset and any(not re.fullmatch(r"[A-Za-z0-9_.-]+", part) or part in {".", ".."}
                               for part in asset["path"].split("/")):
        raise ValueError("Invalid tagged source path")
    if "release_tag" in asset:
        version(asset["release_tag"].removeprefix("v"))


def asset_url(catalog, asset):
    if "path" in asset or asset.get("source_archive"):
        commit = catalog.get("commit", "")
        if not re.fullmatch(r"[a-f0-9]{40}", commit):
            raise ValueError("Invalid release source commit")
        if "path" in asset:
            return f"{RAW_BASE}/{commit}/{asset['path']}"
        return f"{SOURCE_BASE}/{commit}"
    return f"{RELEASE_BASE}/{asset.get('release_tag', catalog['tag'])}/{asset['file']}"


def release_metadata(release):
    _, marker, data = (release.get("body") or "").partition(RELEASE_METADATA)
    if not marker:
        return {}
    payload, closing, _ = data.partition("\n-->")
    if not closing or len(payload) > 256 * 1024:
        raise ValueError("Invalid builder release metadata")
    metadata = json.loads(payload)
    if not isinstance(metadata, dict):
        raise ValueError("Invalid builder release metadata")
    return metadata


def check_catalog(catalog, tag):
    if catalog["schema"] != 1 or catalog["tag"] != tag:
        raise ValueError("Unsupported release catalog")
    if not isinstance(catalog.get("supports_screen", False), bool):
        raise ValueError("Invalid display support in release catalog")
    if version(catalog["launcher"]) != version(tag.removeprefix("v")):
        raise ValueError("Launcher version differs from release tag")
    check_asset(catalog["source"])
    if "esp32s3_sdk" in catalog:
        check_asset(catalog["esp32s3_sdk"])
    for record in catalog.get("precompiled", []):
        check_asset(record["asset"])
        if type(record["screen"]) is not bool or not isinstance(record["modules"], list):
            raise ValueError("Invalid precompiled firmware selection")
        if any(not re.fullmatch(r"[a-z][a-z0-9-]{0,39}", name) for name in record["modules"]):
            raise ValueError("Invalid precompiled firmware module ID")
    for asset in catalog.get("help_assets", {}).values():
        check_asset(asset)
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
        for key in ("help_asset", "setup_guide_asset"):
            if key in module:
                check_asset(module[key])
        for asset in module.get("setup_assets", {}).values():
            check_asset(asset)


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
    catalog = release_metadata(release).get("catalog")
    if catalog is None:
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
    with tempfile.TemporaryDirectory(dir=cache) as staging:
        temporary = Path(staging) / "catalog.json"
        temporary.write_text(json.dumps(catalog), encoding="utf-8")
        temporary.replace(path)
    return catalog


def fetch_asset(catalog, asset, cache=STATE, target=None):
    tag = catalog["tag"]
    version(tag.removeprefix("v"))
    name, checksum = asset["file"], asset["sha256"]
    check_asset(asset)
    path = target or cache / "downloads" / name
    if path.is_file() and path.stat().st_size == asset["size"] and digest(path) == checksum:
        return path
    path.parent.mkdir(parents=True, exist_ok=True)
    legacy = cache / "downloads" / checksum / name
    if legacy.is_file() and legacy.stat().st_size == asset["size"] and digest(legacy) == checksum:
        legacy.replace(path)
        return path
    with tempfile.TemporaryDirectory(dir=path.parent) as staging:
        temporary = Path(staging) / "download"
        with request(asset_url(catalog, asset)) as response, temporary.open("wb") as output:
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
    return path


def source_tree(catalog, cache=STATE):
    asset = catalog["source"]
    checksum = asset["sha256"]
    root = cache / "source" / checksum[:16]
    if root.is_dir():
        if (root / ".source-sha256").read_text(encoding="ascii") != checksum:
            raise ValueError("Cached source directory belongs to a different archive")
        return root
    root.parent.mkdir(parents=True, exist_ok=True)
    archive = fetch_asset(catalog, asset, cache)
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
        if asset.get("source_archive"):
            members = list(tree.iterdir())
            if len(members) != 1 or not members[0].is_dir():
                raise ValueError("GitHub source archive has an unexpected layout")
            tree = members[0]
        embedded = load_modules(tree, include_local=False)
        keys = ("id", "name", "version", "kind")
        actual = [{key: item[key] for key in keys} for item in embedded["modules"]]
        declared = [{key: item[key] for key in keys} for item in catalog["modules"]]
        if (embedded["launcher"] != catalog["launcher"] or actual != declared
                or any(embedded.get(key, False) != catalog.get(key, False)
                       for key in ("supports_screen", "supports_usb_help"))):
            raise ValueError("Release source and catalog differ")
        (tree / ".source-sha256").write_text(checksum, encoding="ascii")
        tree.rename(root)
    return root


def download_app_resources(catalog, selected, include_help, cache=STATE):
    """Fetch optional help and setup files for the selected apps."""
    if catalog is None or not include_help:
        return None
    resources = dict(catalog.get("help_assets", {}))
    for module in catalog["modules"]:
        if module["id"] not in selected:
            continue
        if module.get("help_asset"):
            resources[module["help"]] = module["help_asset"]
        if module.get("setup_guide_asset"):
            resources[Path(module["setup_guide"]).name] = module["setup_guide_asset"]
        resources.update(module.get("setup_assets", {}))
    if not resources:
        return None
    destination = cache / "help" / "current"
    destination.mkdir(parents=True, exist_ok=True)
    for filename, asset in resources.items():
        if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9._-]{0,100}", filename):
            raise ValueError("Invalid help or setup filename")
        target = destination / filename
        previous = cache / "help" / catalog["tag"] / filename
        if not target.exists() and previous.is_file() and digest(previous) == asset["sha256"]:
            previous.replace(target)
        fetch_asset(catalog, asset, cache, target=target)
    return destination


def saved_builds(cache=STATE):
    for path in sorted((cache / "builds").glob("*/build.json"), reverse=True):
        try:
            manifest = json.loads(path.read_text(encoding="utf-8"))
            version(manifest["launcher"])
            if isinstance(manifest["modules"], dict) and all(
                    (path.parent / name).is_file() for name in MIGRATION_FILES.values()):
                yield path.parent, manifest
        except (OSError, ValueError, KeyError, TypeError):
            continue


def build_release(catalog, selected, destination, *, screen, include_help, run,
                  report=print, cache=STATE):
    checksum = catalog["source"]["sha256"]
    modules = {item["id"]: item["version"] for item in catalog["modules"] if item["id"] in selected}
    with project_lock(cache):
        help_root = download_app_resources(catalog, selected, include_help, cache)
        if help_root is not None:
            report(f"Selected app resources are available at {help_root}.")
        for path, manifest in saved_builds(cache):
            if (manifest.get("release_source") != checksum or manifest["modules"] != modules
                    or manifest.get("screen", False) != screen
                    or manifest.get("include_help", False) != include_help):
                continue
            try:
                verify_build(path)
                for name in manifest.get("help_documents", []):
                    relative = f"sdcard/Help/{name}"
                    if Path(name).name != name or digest(path / relative) != manifest["sha256"][relative]:
                        raise ValueError("Saved help checksum mismatch")
            except (OSError, ValueError, KeyError, TypeError):
                continue
            report(f"Reusing verified firmware from {path.name}.")
            return path

        bundled = next((item for item in catalog.get("precompiled", [])
                        if not include_help and set(item["modules"]) == set(selected)
                        and item["screen"] == screen), None)
        if bundled:
            report("Downloading the matching precompiled firmware.")
            archive = fetch_asset(catalog, bundled["asset"], cache)
            with tempfile.TemporaryDirectory(dir=cache) as staging, ZipFile(archive) as package:
                root = Path(staging)
                names = [*MIGRATION_FILES.values(), "build.json"]
                if sum(package.getinfo(name).file_size for name in names) > 9 * 1024**2:
                    raise ValueError("Precompiled firmware exceeds its extraction limit")
                package.extractall(root, names)
                identity = verify_build(root)
                if (identity["launcher"] != catalog["launcher"] or identity["modules"] != modules
                        or identity.get("screen", False) != screen):
                    raise ValueError("Precompiled firmware does not match the selected options")
                identity["release_source"] = checksum
                files = {name: root / name for name in MIGRATION_FILES.values()}
                save_build(destination, files, identity, catalog, selected)
        else:
            report("Preparing source for the selected firmware options.")
            root = source_tree(catalog, cache)
            sdk_archive = None
            installed_sdk = cache / "toolchain/packages" / SDK_PACKAGE
            if ("esp32s3_sdk" in catalog and not os.environ.get("PLATFORMIO_CORE_DIR")
                    and not installed_sdk.exists() and PLATFORM_URL in
                    (root / "research/offline-reader/platformio.ini").read_text(encoding="utf-8")):
                report("Downloading the ESP32-S3 Arduino SDK.")
                sdk_archive = fetch_asset(catalog, catalog["esp32s3_sdk"], cache)
            build(root, selected, destination, run=run, screen=screen, include_help=include_help,
                  help_root=help_root, release_source=checksum, sdk_archive=sdk_archive,
                  include_local=False)
        return destination


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
    package = (local_root / module["source"] if local_root else fetch_asset(
        catalog, module["asset"], cache, target=cache / "downloads" / f"sd-{module['id']}"))
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


def source_asset(relative):
    return dict(describe_asset(ROOT / relative), path=relative)


def prepare_release(tag, firmware, notes):
    from program_update import update_manifest

    version(tag.removeprefix("v"))
    manifest = load_modules(ROOT, include_local=False)
    if tag != "v" + manifest["launcher"]:
        raise ValueError("Tag must match the launcher version")
    commit = subprocess.check_output(["git", "rev-parse", f"{tag}^{{commit}}"], cwd=ROOT, text=True).strip()
    if commit != subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip():
        raise ValueError("Check out the release tag before preparing its metadata")
    with tempfile.TemporaryDirectory() as staging:
        archive = Path(staging) / "firmware-source.zip"
        with request(f"{SOURCE_BASE}/{commit}") as response, archive.open("wb") as stream:
            shutil.copyfileobj(response, stream)
        source = dict(describe_asset(archive), source_archive=True)
    if firmware.name != UNIVERSAL_FIRMWARE:
        raise ValueError("Unexpected universal firmware filename")
    with ZipFile(firmware) as package:
        identity = json.loads(package.read("build.json"))
    if identity["launcher"] != manifest["launcher"]:
        raise ValueError("Universal firmware has the wrong launcher version")
    catalog = {"schema": 1, "tag": tag, "launcher": manifest["launcher"],
               "commit": commit, "source": source, "modules": [], "help_assets": {},
               "precompiled": [{"asset": describe_asset(firmware), "modules": sorted(identity["modules"]),
                                "screen": identity.get("screen", False)}]}
    if PLATFORM_URL in (ROOT / "research/offline-reader/platformio.ini").read_text(encoding="utf-8"):
        catalog["esp32s3_sdk"] = SHARED_SDK
    for filename in ("file-manager.txt", "settings.txt"):
        catalog["help_assets"][filename] = source_asset(f"builder/help/{filename}")
    if manifest.get("supports_screen", False):
        catalog["supports_screen"] = True
    if manifest.get("supports_usb_help", False):
        catalog["supports_usb_help"] = True
    for module in manifest["modules"]:
        entry = {key: module[key] for key in ("id", "name", "version", "kind")}
        entry["description"] = module.get("description", "")
        entry["introduced"] = module.get("introduced", module["version"])
        for key in ("requires_setup", "setup_guide", "help"):
            if key in module:
                entry[key] = module[key]
        if module.get("help"):
            entry["help_asset"] = source_asset(f"builder/help/{module['help']}")
        if module.get("setup_guide"):
            entry["setup_guide_asset"] = source_asset(module["setup_guide"])
        if module.get("setup_assets"):
            entry["setup_assets"] = {name: source_asset(f"builder/help/{name}") for name in module["setup_assets"]}
        if module.get("requires"):
            entry["requires"] = module["requires"]
        if module["kind"] == "sd":
            entry.update(filename=module["filename"], asset=source_asset(module["source"]))
        catalog["modules"].append(entry)
    check_catalog(catalog, tag)
    metadata = json.dumps({"catalog": catalog, "update": update_manifest(ROOT, tag, commit)})
    if "-->" in metadata:
        raise ValueError("Release metadata contains a comment terminator")
    notes.write_text(
        "Download the Windows builder ZIP or the universal firmware ZIP. "
        "The source archives include Linux builder instructions, app help, and setup scripts.\n\n"
        + RELEASE_METADATA + metadata + "\n-->\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tag")
    parser.add_argument("firmware", type=Path)
    parser.add_argument("notes", type=Path)
    args = parser.parse_args()
    prepare_release(args.tag, args.firmware, args.notes)
