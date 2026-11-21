"""Build a selected module set using the checkout's pinned PlatformIO inputs."""

import argparse
from configparser import ConfigParser
from contextlib import contextmanager
import ctypes
import hashlib
import io
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys

ROOT = Path(os.environ.get("CARDPUTER_BUILDER_ROOT", Path(__file__).resolve().parents[1]))
STATE = ROOT / "workspace"
PYTHON = Path(sys.executable)
if PYTHON.name.lower() == "pythonw.exe":
    PYTHON = PYTHON.with_name("python.exe")
NO_CONSOLE = getattr(subprocess, "CREATE_NO_WINDOW", 0)
ENVIRONMENT = "unified-custom-incremental"
APP_CAPACITY = 0x7E0000


def available_memory():
    if os.name == "nt":
        # MEMORYSTATUSEX contains two DWORDs followed by seven 64-bit fields.
        memory = (ctypes.c_uint64 * 8)()
        memory[0] = ctypes.sizeof(memory)
        if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(memory)):
            return memory[2]
    else:
        try:
            for line in Path("/proc/meminfo").read_text(encoding="ascii").splitlines():
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) * 1024
        except OSError:
            pass
    return None


def compiler_jobs(cpu_count, free_bytes):
    reserved = min(4, max(1, cpu_count // 8))
    jobs = max(1, cpu_count - reserved)
    if free_bytes is not None:
        jobs = min(jobs, max(1, (free_bytes - 512 * 1024**2) // (512 * 1024**2)))
    return jobs


def object_directory(project):
    return project / ".pio/builder-objects" / digest(project / "platformio.ini")[:16]


def write_changed(path, contents, encoding="utf-8"):
    if not path.is_file() or path.read_text(encoding=encoding) != contents:
        path.write_text(contents, encoding=encoding)


@contextmanager
def project_lock(project):
    state = project / ".pio"
    state.mkdir(exist_ok=True)
    with (state / "builder.lock").open("a+b") as lock:
        lock.seek(0)
        try:
            if os.name == "nt":
                import msvcrt
                msvcrt.locking(lock.fileno(), msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except OSError as error:
            raise RuntimeError("Another builder instance is using this source tree.") from error
        yield


def digest(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_modules(root):
    catalog = json.loads((root / "builder/modules.json").read_text(encoding="utf-8"))
    if catalog["schema"] != 1:
        raise ValueError("Unsupported module catalog")
    ids = set()
    for record in [{"id": "launcher", "version": catalog["launcher"]}, *catalog["modules"]]:
        name, version = record["id"], record["version"]
        if name in ids or not re.fullmatch(r"[a-z][a-z0-9-]{0,39}", name):
            raise ValueError(f"Invalid or repeated module ID: {name}")
        if not re.fullmatch(r"\d+\.\d+\.\d+", version, flags=re.ASCII):
            raise ValueError(f"Invalid version for {name}")
        ids.add(name)
    for record in catalog["modules"]:
        if record["kind"] not in {"native", "sd"}:
            raise ValueError(f"Unsupported package type for {record['id']}")
    components = catalog["components"]
    for name in components:
        if not re.fullmatch(r"[a-z][a-z0-9-]{0,39}", name) or name in ids:
            raise ValueError(f"Invalid shared component ID: {name}")
    resolve_components(components, [{"requires": list(components)}, *catalog["modules"]])
    return catalog


def resolve_components(components, modules):
    selected = {}
    visiting = set()

    def add(name):
        if name in visiting:
            raise ValueError(f"Shared component dependency cycle at {name}")
        if name in selected:
            return
        if name not in components:
            raise ValueError(f"Unknown shared component: {name}")
        visiting.add(name)
        component = components[name]
        for required in component.get("requires", []):
            add(required)
        visiting.remove(name)
        selected[name] = component

    for module in modules:
        for required in module.get("requires", []):
            add(required)
    return selected


def generate(project, catalog, selected, screen=False, include_help=False):
    modules = {module["id"]: module for module in catalog["modules"] if module["kind"] == "native"}
    unknown = set(selected) - modules.keys()
    if unknown:
        raise ValueError("Unknown native modules: " + ", ".join(sorted(unknown)))
    active = [modules[name] for name in modules if name in selected]
    identity = {
        "launcher": catalog["launcher"],
        "modules": {module["id"]: module["version"] for module in active},
    }
    if include_help:
        identity["include_help"] = True
    components = resolve_components(catalog["components"], active)
    if include_help:
        flags = ["-DUNIFIED_HELP_MENU=1"]
    else:
        flags = []
    config = ConfigParser(interpolation=None)
    config.read(project / "platformio.ini", encoding="utf-8")
    config["platformio"]["core_dir"] = str(Path(os.environ.get(
        "PLATFORMIO_CORE_DIR", str(STATE / "toolchain"))).resolve())
    config["platformio"]["libdeps_dir"] = str(project / ".pio/builder-libdeps")
    config["platformio"]["build_dir"] = str(object_directory(project))
    sources = ["+<unified.cpp>", "+<headless_board.cpp>", "+<unified_hid.cpp>"]
    flags = [*flags,
        "${env:backgroundaudio-minimal.build_flags}", "-Os", "-DARDUINO_USB_MODE=0",
        "-DUNIFIED_RESERVED_PHONEMES=1", "-I generated", "-DUNIFIED_CUSTOM_BUILD=1",
        '-I "${platformio.core_dir}/packages/framework-arduinoespressif32/libraries/WiFi/src"',
        '-I "${platformio.core_dir}/packages/framework-arduinoespressif32/libraries/NetworkClientSecure/src"',
    ]
    libraries = ["symlink://generated/backgroundaudio-trial"]
    if screen:
        identity["screen"] = True
        flags.append("-DUNIFIED_SCREEN=1")
        libraries.append("symlink://../M5GFX")
    app_flags = []
    scripts = ["${env.extra_scripts}"]
    registrations = []
    for module in [*components.values(), *active]:
        for source in module.get("sources", []):
            if not re.fullmatch(r"[\w-]+\.cpp", source) or not (project / "src" / source).is_file():
                raise ValueError(f"Invalid module source: {source}")
            sources.append(f"+<{source}>")
        for flag in module.get("flags", []):
            (app_flags if flag.startswith("-D") else flags).append(flag)
        libraries.extend(module.get("libraries", []))
        scripts.extend(module.get("scripts", []))
        if "entry" in module:
            entry = module["entry"]
            if not re.fullmatch(r"[A-Za-z_]\w*", entry, flags=re.ASCII):
                raise ValueError(f"Invalid launcher entry: {entry}")
            label = json.dumps(module["name"], ensure_ascii=False)
            registrations.append(f"UNIFIED_ADD_APP({entry}, {label})\n")
    config[f"env:{ENVIRONMENT}"] = {
        "extends": "env:unified-dev",
        "board_build.partitions": "partitions-playpalace.csv",
        "board_upload.maximum_size": str(APP_CAPACITY),
        "build_src_filter": " ".join(dict.fromkeys(sources)),
        "build_unflags": "-O3 -DARDUINO_USB_MODE=1 -DUNIFIED_DIAGNOSTICS=1",
        "build_flags": "\n" + "\n".join(dict.fromkeys(flags)),
        "build_src_flags": "\n" + "\n".join(dict.fromkeys(app_flags)),
        "lib_deps": "\n" + "\n".join(dict.fromkeys(libraries)),
        "lib_compat_mode": "off",
        "extra_scripts": "\n" + "\n".join(dict.fromkeys(scripts)),
    }
    config_path = project / ".pio" / "builder.ini"
    config_path.parent.mkdir(exist_ok=True)
    stream = io.StringIO()
    config.write(stream)
    write_changed(config_path, stream.getvalue())
    generated = project / "generated"
    generated.mkdir(exist_ok=True)
    encoded = json.dumps(identity, separators=(",", ":"), ensure_ascii=True)
    write_changed(generated / "builder_identity.h",
                  "#pragma once\n#define UNIFIED_BUILD_IDENTITY " + json.dumps(encoded) + "\n", "ascii")
    write_changed(generated / "builder_apps.inc", "".join(registrations))
    return config_path, dict(identity, components=list(components)), Path(config["platformio"]["core_dir"])


def verify_linked_codecs(artifacts, core, components):
    tool = core / "packages/toolchain-xtensa-esp-elf/bin/xtensa-esp32s3-elf-nm"
    if os.name == "nt":
        tool = tool.with_suffix(".exe")
    if not tool.is_file():
        tool = tool.with_name(tool.name.replace("xtensa-esp32s3-", "xtensa-"))
    symbols = subprocess.check_output([str(tool), "-C", "--defined-only",
                                       str(artifacts / "firmware.elf")], text=True)
    for component, symbol in (("mp3-codec", "mad_frame_decode"), ("vorbis-codec", "OggVorbisDecoder::")):
        linked = symbol in symbols
        if linked != (component in components):
            raise ValueError(f"{component} linker output does not match the selected components")
        print(f"{component}: {'linked' if linked else 'omitted'}", flush=True)


def build(root, selected, destination, run=subprocess.check_call, fetch=True, check_codecs=False,
          screen=False, include_help=False):
    root = root.resolve()
    destination = destination.resolve()
    catalog = load_modules(root)
    project = root / "research/offline-reader"
    with project_lock(project):
        return build_locked(root, project, catalog, selected, destination, run, fetch, check_codecs,
                            screen, include_help)


def build_locked(root, project, catalog, selected, destination, run, fetch, check_codecs, screen=False,
                 include_help=False):
    config, identity, core = generate(project, catalog, selected, screen, include_help)
    if fetch:
        dependencies = [str(PYTHON), "-u", str(root / "fetch_dependencies.py"), "--firmware"]
        if "bluetooth-usb-keyboard" in selected:
            dependencies.append("--keyboard")
        run(dependencies, cwd=root)
        if screen:
            run([str(PYTHON), "-u", str(root / "fetch_dependencies.py"), "M5GFX"], cwd=root)
    # PlatformIO resolves local libraries before running its pre-build scripts.
    run([str(PYTHON), str(project / "prepare_background.py")], cwd=project)
    cpu_count = getattr(os, "process_cpu_count", os.cpu_count)() or 2
    jobs = compiler_jobs(cpu_count, available_memory())
    print(f"PlatformIO will use {jobs} compiler jobs on {cpu_count} available logical CPUs.", flush=True)
    run([str(PYTHON), "-u", "-m", "platformio", "run", "--project-dir", str(project),
         "--project-conf", str(config), "--disable-auto-clean", "-e", ENVIRONMENT,
         "-j", str(jobs)], cwd=project)
    artifacts = object_directory(project) / ENVIRONMENT
    if check_codecs:
        verify_linked_codecs(artifacts, core, identity["components"])
    files = {name: artifacts / name for name in ("firmware.bin", "partitions.bin", "bootloader.bin")}
    files["boot_app0.bin"] = (
        core / "packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin")
    if not 0 < files["firmware.bin"].stat().st_size <= APP_CAPACITY:
        raise ValueError("Firmware exceeds the large application partition")
    destination.mkdir(parents=True, exist_ok=False)
    for name, source in files.items():
        shutil.copyfile(source, destination / name)
    if include_help:
        help_modules = [module for module in catalog["modules"]
                        if module["id"] in selected and module.get("help")]
        help_dir = destination / "sdcard" / "Help"
        help_dir.mkdir(parents=True)
        filenames = ["file-manager.txt", "settings.txt"]
        filenames.extend(module["help"] for module in help_modules)
        for filename in dict.fromkeys(filenames):
            if not re.fullmatch(r"[A-Za-z0-9_-]+\.txt", filename):
                raise ValueError(f"Invalid help filename: {filename}")
            source = root / "builder" / "help" / filename
            if not source.is_file():
                raise ValueError(f"Missing help document: {source}")
            shutil.copyfile(source, help_dir / filename)
        identity["help_documents"] = list(dict.fromkeys(filenames))
    identity["sha256"] = {name: digest(destination / name) for name in files}
    if include_help:
        identity["sha256"].update({
            path.relative_to(destination).as_posix(): digest(path)
            for path in (destination / "sdcard" / "Help").glob("*.txt")
        })
    (destination / "build.json").write_text(json.dumps(identity, indent=2), encoding="utf-8")
    return identity


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("modules", nargs="*")
    parser.add_argument("--preset", help="core, all, or a single module ID")
    parser.add_argument("--skip-fetch", action="store_true")
    parser.add_argument("--check-codecs", action="store_true", help="Check codec presence in linked firmware")
    parser.add_argument("--screen", action="store_true", help="Keep the LCD on and show spoken text")
    parser.add_argument("--include-help", action="store_true",
                        help="Package help text for the selected apps under output/sdcard/Help")
    parser.add_argument("--keyboard-learning", action="store_true",
                        help="Include the Keyboard learning program in the Help menu")
    args = parser.parse_args()
    selected = args.modules
    if args.preset and selected:
        parser.error("Choose a preset or list explicit modules")
    if args.preset == "all":
        selected = [item["id"] for item in load_modules(ROOT)["modules"] if item["kind"] == "native"]
    elif args.preset and args.preset != "core":
        selected = [args.preset]
    if args.keyboard_learning:
        selected.append("key-learning")
    selected = list(dict.fromkeys(selected))
    build(ROOT, selected, args.output, fetch=not args.skip_fetch, check_codecs=args.check_codecs,
          screen=args.screen, include_help=args.include_help)
