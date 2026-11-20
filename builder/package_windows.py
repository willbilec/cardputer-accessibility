"""Package the x64 Windows builder with the active virtual environment's dependencies."""

import argparse
import json
import os
from pathlib import Path
import shutil
import site
import subprocess
import sys
from zipfile import ZIP_DEFLATED, ZipFile

from build import ROOT, load_modules
from program_update import PROGRAM_FILES, RUNTIME_ABI
from source_files import firmware_files


def package(output, with_source=False):
    if sys.platform != "win32" or sys.prefix == sys.base_prefix or sys.maxsize <= 2**32:
        raise SystemExit("Run with a 64-bit Windows Python virtual environment.")
    base = Path(sys.base_prefix)
    compiler = Path(os.environ["WINDIR"]) / "Microsoft.NET/Framework64/v4.0.30319/csc.exe"
    if not compiler.is_file():
        raise SystemExit("The Windows .NET Framework C# compiler is required.")
    output = output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    runtime = output / "runtime"
    runtime.mkdir()
    for name in ("python.exe", "pythonw.exe", "python3.dll", "LICENSE.txt"):
        shutil.copyfile(base / name, runtime / name)
    for pattern in ("python[0-9][0-9][0-9].dll", "vcruntime*.dll"):
        for library in base.glob(pattern):
            shutil.copyfile(library, runtime / library.name)
    excluded = shutil.ignore_patterns("__pycache__", "*.pyc", "site-packages", "test", "tests", "idlelib")
    shutil.copytree(base / "DLLs", runtime / "DLLs", ignore=excluded)
    shutil.copytree(base / "Lib", runtime / "Lib", ignore=excluded)
    dependencies = Path(site.getsitepackages()[-1])
    shutil.copytree(dependencies, runtime / "Lib/site-packages",
                    ignore=shutil.ignore_patterns("__pycache__", "*.pyc", "test", "tests"))

    files = {"LICENSE", "BUILDING.md", "FLASHING.md"}
    files.update("builder/" + name for name in (*PROGRAM_FILES, "bootstrap.py"))
    if with_source:
        files.update(firmware_files(ROOT, load_modules(ROOT), sd_data=True))
    for filename in sorted(files):
        source = ROOT / filename
        if source.is_file():
            target = output / filename
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(source, target)
    (output / "builder/program.json").write_text(json.dumps({
        "version": load_modules(ROOT)["launcher"], "runtime_abi": RUNTIME_ABI,
    }), encoding="utf-8")
    subprocess.check_call([
        str(compiler), "/nologo", "/target:winexe", "/platform:x64",
        "/reference:System.Windows.Forms.dll",
        f"/out:{output / 'Cardputer ADV Firmware Builder.exe'}",
        str(ROOT / "builder/WindowsLauncher.cs"),
    ])
    archive = output.with_name(output.name + ".zip")
    with ZipFile(archive, mode="x", compression=ZIP_DEFLATED, compresslevel=5) as package_file:
        for path in sorted(output.rglob("*")):
            if path.is_file():
                package_file.write(path, path.relative_to(output.parent))
    print(archive)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--with-source", action="store_true", help="Include the production sources for local testing")
    args = parser.parse_args()
    package(args.output, args.with_source)
