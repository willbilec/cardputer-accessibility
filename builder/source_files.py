"""Files required by the production firmware build."""

from pathlib import Path
import re


def firmware_files(root, manifest, *, sd_data=False):
    firmware = Path("research/offline-reader")
    files = {
        Path("LICENSE"), Path("fetch_dependencies.py"), Path("research/dependencies.json"),
        Path("builder/modules.json"),
    }
    files.update(firmware / name for name in (
        "platformio.ini", "partitions.csv", "partitions-playpalace.csv",
        "prepare.py", "prepare_background.py", "prepare_hid.py",
    ))
    sources = {firmware / "src" / name for name in (
        "unified.cpp", "headless_board.cpp", "unified_hid.cpp",
    )}
    for record in [*manifest["components"].values(),
                   *(module for module in manifest["modules"] if not module.get("local_only"))]:
        sources.update(firmware / "src" / name for name in record.get("sources", []))
        files.update(firmware / script.split(":", 1)[-1] for script in record.get("scripts", []))
        if sd_data and record.get("kind") == "sd":
            files.add(Path(record["source"]))
    includes = re.compile(r'^\s*#\s*include\s+["<]([^">]+)[">]', re.MULTILINE)
    while sources:
        source = sources.pop()
        if source in files:
            continue
        files.add(source)
        for name in includes.findall((root / source).read_text(encoding="utf-8")):
            for parent in (source.parent, firmware / "include"):
                header = parent / name
                if ".." not in header.parts and (root / header).is_file():
                    sources.add(header)
                    break
    files.update(path.relative_to(root) for path in (root / firmware / "licenses").glob("*") if path.is_file())
    return sorted(str(path.as_posix()) for path in files)
