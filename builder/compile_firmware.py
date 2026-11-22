"""Run PlatformIO for builder-managed ESP32-S3 release builds."""

import json
from pathlib import Path
import sys
import tempfile
from zipfile import ZipFile

UPSTREAM_VERSION = "55.03.39"
PLATFORM_URL = ("https://github.com/pioarduino/platform-espressif32/releases/download/"
                f"{UPSTREAM_VERSION}/platform-espressif32.zip")
EXTRA_TOOLS = {"tool-xtensa-esp-elf-gdb", "contrib-piohome"}
SDK_PACKAGE = "framework-arduinoespressif32-libs"
SDK_FULL = "https://github.com/espressif/arduino-esp32/releases/download/3.3.9/esp32-core-3.3.9-libs.tar.xz"
SDK_ASSET = "cardputer-adv-esp32s3-sdk.zip"


def install_s3_sdk(packages, spec, archive):
    from platformio.package.meta import PackageMetadata

    destination = packages / SDK_PACKAGE
    print("Installing the ESP32-S3 Arduino SDK.", flush=True)
    packages.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=packages.parent) as staging:
        stage = Path(staging)
        with ZipFile(archive) as package:
            if any(Path(entry.filename).is_absolute() or ".." in Path(entry.filename).parts
                   or "\\" in entry.filename or ":" in entry.filename for entry in package.infolist()):
                raise ValueError("Invalid ESP32-S3 SDK file path")
            if sum(entry.file_size for entry in package.infolist()) > 768 * 1024**2:
                raise ValueError("ESP32-S3 SDK exceeds its extraction limit")
            package.extractall(stage)
        if not (stage / "esp32s3/pioarduino-build.py").is_file():
            raise ValueError("ESP32-S3 SDK is missing its PlatformIO build script")
        manifest = json.loads((stage / "package.json").read_text(encoding="utf-8"))
        if manifest["name"] != SDK_PACKAGE:
            raise ValueError("ESP32-S3 SDK package name differs")
        # The SCons worker resolves the same upstream package spec in a separate
        # process. Register the byte-identical ESP32-S3 subtree with that spec.
        PackageMetadata("tool", SDK_PACKAGE, manifest["version"], spec).dump(stage / ".piopm")
        stage.rename(destination)


def release_platform(name, path):
    module = original_loader(name, path)
    platform = getattr(module, "Espressif32Platform", None)
    if name != "espressif32" or platform is None:
        return module

    class ReleasePlatform(platform):
        def install_tool(self, name):
            if name in EXTRA_TOOLS and str(self.version) == UPSTREAM_VERSION:
                self.packages[name]["optional"] = True
                return True
            return super().install_tool(name)

        def configure_default_packages(self, variables, targets):
            if str(self.version) == UPSTREAM_VERSION:
                module.install_freertos_gdb = lambda *_: None
            return super().configure_default_packages(variables, targets)

        def install_required_packages(self, force=False):
            spec = self.get_package_spec(SDK_PACKAGE) if SDK_PACKAGE in self.packages else None
            if (sdk_archive and self.version == UPSTREAM_VERSION and spec and spec.uri == SDK_FULL
                    and not self.packages[SDK_PACKAGE].get("optional")
                    and not (Path(self.packages_dir) / SDK_PACKAGE).exists()):
                install_s3_sdk(Path(self.packages_dir), spec, sdk_archive)
            return super().install_required_packages(force=force)

    module.Espressif32Platform = ReleasePlatform
    return module


if __name__ == "__main__":
    from platformio.__main__ import main
    from platformio.platform.factory import PlatformFactory

    original_loader = PlatformFactory.load_platform_module
    PlatformFactory.load_platform_module = staticmethod(release_platform)
    sdk_archive = None
    if sys.argv[1:2] == ["--sdk-archive"]:
        sdk_archive = Path(sys.argv[2])
        del sys.argv[1:3]
    sys.exit(main())
