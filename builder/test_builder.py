"""Regression tests for release handling and flash write boundaries."""

import hashlib
import io
import json
from pathlib import Path
import struct
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from zipfile import ZipFile

from build import (APP_CAPACITY, ENVIRONMENT, ROOT, build_locked, compiler_jobs, digest, generate,
                   load_modules, resolve_components, trim_compiler_objects, trim_saved_builds)
from catalog import (RELEASE_METADATA, build_release, describe_asset, device_update_summary, fetch_asset, install_sd,
                     installed_version, refresh, source_tree)
from flash import FLASH_BYTES, MIGRATION_FILES, check_layout, copy_help_to_card, find_download_port, flash, verify_build
from source_files import firmware_files
from progress import output_events
from program_update import check_update, install_update, update_manifest


def partition_table():
    entries = [
        (1, 2, 0x9000, 0x5000, b"nvs"),
        (1, 0, 0xE000, 0x2000, b"otadata"),
        (0, 0, 0x10000, APP_CAPACITY, b"reader"),
        (1, 3, 0x7F0000, 0x10000, b"coredump"),
    ]
    body = b"".join(struct.pack("<HBBII16sI", 0x50AA, *entry, 0) for entry in entries)
    checksum = hashlib.md5(body, usedforsecurity=False).digest()
    return (body + b"\xEB\xEB" + b"\xFF" * 14 + checksum).ljust(0xC00, b"\xFF")


class BuilderTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_clean_build_prepares_audio_before_platformio_resolves_libraries(self):
        commands = []

        def run(args, *, cwd):
            commands.append(args[2] if args[1] == "-u" else args[1])
            if "-m" in args:
                raise StopIteration

        project = self.root / "firmware"
        generated = project / "builder.ini"
        with patch("build.generate", return_value=(generated, {}, self.root)):
            with self.assertRaises(StopIteration):
                build_locked(self.root, project, {}, [], self.root / "output", run, True, False)
        self.assertEqual(commands, [
            str(self.root / "fetch_dependencies.py"),
            str(project / "prepare_background.py"),
            "-m",
        ])

    def test_config_includes_only_selected_module_inputs(self):
        project = self.root / "project"
        project.mkdir()
        original = ROOT / "research/offline-reader"
        (project / "platformio.ini").write_bytes((original / "platformio.ini").read_bytes())
        (project / "src").mkdir()
        for name in ("nvda_remote.cpp", "nvda_tls.cpp"):
            (project / "src" / name).touch()
        config, identity, _ = generate(project, load_modules(ROOT), ["nvda-remote"])
        self.assertEqual(list(identity["modules"]), ["nvda-remote"])
        section = config.read_text(encoding="utf-8").split(f"[env:{ENVIRONMENT}]", 1)[1]
        self.assertIn("+<nvda_remote.cpp>", section)
        self.assertNotIn("+<playpalace.cpp>", section)
        self.assertNotIn("micro-vorbis", section)
        self.assertNotIn("+<media_decoder.cpp>", section)
        self.assertNotIn("M5GFX", section)
        self.assertNotIn("UNIFIED_SCREEN", section)
        self.assertNotIn("screen", identity)
        self.assertEqual(identity["components"], ["json"])
        self.assertNotIn("UNIFIED_DIAGNOSTICS=1", section.split("build_flags", 1)[1])
        with self.assertRaisesRegex(ValueError, "Unknown native"):
            generate(project, load_modules(ROOT), ["nonexistent"])

    def test_screen_build_adds_display_driver_and_identity(self):
        project = self.root / "project"
        project.mkdir()
        original = ROOT / "research/offline-reader"
        (project / "platformio.ini").write_bytes((original / "platformio.ini").read_bytes())
        config, identity, _ = generate(project, load_modules(ROOT), [], screen=True)
        section = config.read_text(encoding="utf-8").split(f"[env:{ENVIRONMENT}]", 1)[1]
        self.assertIn("-DUNIFIED_SCREEN=1", section)
        self.assertIn("symlink://../M5GFX", section)
        self.assertIs(identity["screen"], True)

    def test_shared_components_follow_only_selected_consumers(self):
        catalog = load_modules(ROOT)
        components = catalog["components"]
        modules = {item["id"]: item for item in catalog["modules"]}
        def selected(*names):
            return set(resolve_components(components, [modules[name] for name in names]))
        self.assertEqual(selected(), set())
        self.assertEqual(selected("bible"), {"media-decoder", "file-audio-pipeline", "mp3-codec"})
        self.assertEqual(selected("playpalace"), {"media-decoder", "vorbis-codec", "json"})
        self.assertEqual(selected("bible", "playpalace", "nvda-remote"),
                         {"media-decoder", "file-audio-pipeline", "mp3-codec", "vorbis-codec", "json"})
        with self.assertRaisesRegex(ValueError, "dependency cycle"):
            resolve_components({"cycle": {"requires": ["cycle"]}}, [{"requires": ["cycle"]}])
        with self.assertRaisesRegex(ValueError, "Unknown shared component"):
            resolve_components({}, [{"requires": ["unknown"]}])

    def test_compiler_parallelism_leaves_cpu_and_memory_headroom(self):
        self.assertEqual(compiler_jobs(1, None), 1)
        self.assertEqual(compiler_jobs(16, 16 * 1024**3), 14)
        self.assertEqual(compiler_jobs(32, 3 * 1024**3), 5)
        self.assertEqual(compiler_jobs(4, 512 * 1024**2), 1)

    def test_release_source_includes_transitive_headers_without_research_experiments(self):
        files = firmware_files(ROOT, load_modules(ROOT))
        self.assertIn("research/offline-reader/include/file_playback_formats.h", files)
        self.assertIn("research/offline-reader/src/micro_vorbis_demux.cpp", files)
        self.assertIn("research/offline-reader/src/unified_bible.inc", files)
        self.assertIn("research/offline-reader/optimize_vorbis.py", files)
        self.assertNotIn("research/offline-reader/UNIFIED_CONTROLS.md", files)
        self.assertFalse(any("/test/" in path or "speech-prototype" in path for path in files))

    def test_progress_is_live_and_logs_only_the_completed_transfer(self):
        chunks = iter([b"Downloading 0% 10% 2", b"0% 30%", b" 100%\nUnpacking 0% 50%",
                       b" 100%\r\nCompiling\n", b""])
        stream = SimpleNamespace(read1=lambda _: next(chunks))
        result = list(output_events(stream))
        self.assertIn(("progress", "Downloading: 10%"), result)
        self.assertIn(("progress", "Downloading: 30%"), result)
        self.assertIn(("progress", "Unpacking: 50%"), result)
        self.assertEqual([line for kind, line in result if kind == "log"],
                         ["Downloading: 100%", "Unpacking: 100%", "Compiling"])

    def test_startup_status_counts_firmware_and_apps_and_lists_new_apps(self):
        catalog = {"launcher": "0.3.0", "modules": [
            {"id": "reader", "name": "Reader", "kind": "native", "version": "0.3.0"},
            {"id": "timer", "name": "Timer", "kind": "native", "version": "0.3.0"},
            {"id": "old", "name": "Previously offered", "kind": "native", "version": "0.2.0"},
        ]}
        report = device_update_summary({"launcher": "0.2.0", "modules": {"reader": "0.2.0"}}, catalog)
        self.assertIn("2 updates are available.", report)
        self.assertIn("New apps available: Timer.", report)
        self.assertNotIn("Previously offered", report)
        current = {"launcher": "0.3.0", "modules": {"reader": "0.3.0"}}
        self.assertIn("all installed apps are up to date", device_update_summary(current, catalog))
        self.assertIn("missing from this catalog", device_update_summary(
            {"launcher": "0.3.0", "modules": {"missing": "0.3.0"}}, catalog))
        self.assertIn("did not report", device_update_summary(None, catalog))

    def test_installed_update_switches_only_the_application_overlay(self):
        state = self.root / "workspace"
        state.mkdir()
        firmware = state / "saved-firmware.bin"
        firmware.write_bytes(b"existing firmware")
        record = update_manifest(ROOT, "v0.3.0")

        def download_file(url):
            name = url.rsplit("/builder/", 1)[1]
            return io.BytesIO((ROOT / "builder" / name).read_bytes())

        with patch("program_update.request", side_effect=download_file):
            install_update(record, state)
        self.assertEqual(json.loads((state / "program-update.json").read_text())["version"], "0.3.0")
        self.assertTrue((state / "program/0.3.0/app.py").is_file())
        self.assertEqual(firmware.read_bytes(), b"existing firmware")

    def test_sd_retry_and_cache(self):
        data = b"package contents"
        asset = {"file": "controls.txt", "size": len(data),
                 "sha256": hashlib.sha256(data).hexdigest()}
        catalog = {"tag": "v0.2.0"}
        module = {"id": "guide", "version": "0.2.0", "asset": asset, "filename": "Controls.txt"}
        card, cache = self.root / "card", self.root / "cache"
        card.mkdir()
        original = card / "Personal.txt"
        original.write_text("Keep me", encoding="utf-8")
        with patch("catalog.request", return_value=io.BytesIO(b"x" * len(data))):
            with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
                install_sd(card, catalog, module, cache)
        self.assertIsNone(installed_version(card, module))
        with patch("catalog.request", return_value=io.BytesIO(data)):
            self.assertTrue(install_sd(card, catalog, module, cache))
        with patch("catalog.request", side_effect=AssertionError("Network should not be used")):
            self.assertTrue((cache / "downloads/sd-guide").is_file())
            self.assertFalse(install_sd(card, catalog, module, cache))
        self.assertEqual(installed_version(card, module), "0.2.0")
        self.assertEqual(original.read_text(encoding="utf-8"), "Keep me")

    def test_asset_update_keeps_one_verified_copy(self):
        def asset(data):
            return {"file": "guide.txt", "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}

        catalog = {"tag": "v0.2.0"}
        with patch("catalog.request", return_value=io.BytesIO(b"first")):
            original = fetch_asset(catalog, asset(b"first"), self.root)
        with patch("catalog.request", return_value=io.BytesIO(b"wrong")):
            with self.assertRaisesRegex(ValueError, "Checksum mismatch"):
                fetch_asset(catalog, asset(b"later"), self.root)
        self.assertEqual(original.read_bytes(), b"first")
        with patch("catalog.request", return_value=io.BytesIO(b"later")):
            self.assertEqual(fetch_asset(catalog, asset(b"later"), self.root), original)
        self.assertEqual(list(original.parent.iterdir()), [original])
        self.assertEqual(original.read_bytes(), b"later")

    def test_precompiled_release_and_exact_reuse_skip_source_and_compiler(self):
        archive = self.root / "release.zip"
        files = {"firmware.bin": b"firmware", "partitions.bin": partition_table(),
                 "bootloader.bin": b"bootloader", "boot_app0.bin": bytes(0x2000)}
        source = {"file": "source.zip", "size": 100, "sha256": "a" * 64}
        identity = {"launcher": "0.2.0", "modules": {"reader": "0.2.1"}, "screen": True}
        identity["sha256"] = {name: hashlib.sha256(payload).hexdigest() for name, payload in files.items()}
        with ZipFile(archive, "w") as bundle:
            for name in MIGRATION_FILES.values():
                bundle.writestr(name, files[name])
            bundle.writestr("build.json", json.dumps(identity))
        catalog = {"tag": "v0.2.0", "launcher": "0.2.0", "source": source,
                   "modules": [{"id": "reader", "version": "0.2.1", "kind": "native"}],
                   "precompiled": [{"asset": describe_asset(archive), "modules": ["reader"], "screen": True}]}
        expected = self.root / "builds/first"
        later = self.root / "builds/later"
        def no_compiler(*args, **kwargs):
            self.fail("The build should not run the compiler")
        options = dict(screen=True, include_help=False, run=no_compiler, cache=self.root)
        with patch("catalog.request", return_value=io.BytesIO(archive.read_bytes())) as fetch:
            self.assertEqual(build_release(catalog, ["reader"], expected, **options), expected)
            self.assertEqual(fetch.call_count, 1)
        self.assertEqual(verify_build(expected)["release_source"], source["sha256"])
        with patch("catalog.request", side_effect=AssertionError("No network needed")):
            self.assertEqual(build_release(catalog, ["reader"], later, **options), expected)
        self.assertFalse(later.exists())

    def test_download_mode_follows_windows_interface_even_when_ids_differ(self):
        application = SimpleNamespace(device="COM6", serial_number="B81F3F6FFE30", location="1-8:x.1")
        rom = SimpleNamespace(device="COM4", serial_number="B8:1F:3F:6F:FE:30", location="1-8:x.0")
        other = SimpleNamespace(device="COM7", serial_number="F0:12:34:56:78:90", location="1-9:x.0")
        with (patch("flash.ports", side_effect=[[application], [other, rom]]),
              patch("flash.serial.Serial") as touch,
              patch("flash.time.sleep"),
              patch("flash.identify", return_value="b8:1f:3f:6f:fe:30") as identify):
            self.assertEqual(find_download_port("COM6"), ("COM4", "b8:1f:3f:6f:fe:30"))
        touch.assert_called_once_with("COM6", 1200, timeout=0.25)
        identify.assert_called_once_with("COM4")

    def test_release_source_is_verified_and_reused_offline(self):
        manifest = load_modules(ROOT)
        records = [{key: item[key] for key in ("id", "name", "version", "kind")}
                   for item in manifest["modules"]]
        archive = self.root / "source.zip"
        with ZipFile(archive, "w") as source:
            source.writestr("builder/modules.json", json.dumps(manifest))
            source.writestr("fetch_dependencies.py", "# archived dependency script")
        catalog = {"tag": "v" + manifest["launcher"], "launcher": manifest["launcher"],
                   "modules": records, "source": describe_asset(archive),
                   "supports_screen": manifest["supports_screen"],
                   "supports_usb_help": manifest["supports_usb_help"]}
        cache = self.root / "cache"
        with patch("catalog.request", return_value=io.BytesIO(archive.read_bytes())):
            tree = source_tree(catalog, cache)
        self.assertEqual(load_modules(tree), manifest)
        with patch("catalog.request", side_effect=AssertionError("Network should not be used")):
            self.assertEqual(source_tree(catalog, cache), tree)

    def test_compact_release_metadata_and_tagged_source_downloads(self):
        tag, commit = "v0.3.0", "b" * 40
        manifest = load_modules(ROOT)
        manifest["launcher"] = "0.3.0"
        archive = self.root / "tagged-source.zip"
        with ZipFile(archive, "w") as source:
            source.writestr(f"checkout-{commit}/builder/modules.json", json.dumps(manifest))
        catalog = {
            "schema": 1, "tag": tag, "launcher": manifest["launcher"], "commit": commit,
            "source": dict(describe_asset(archive), source_archive=True),
            "supports_screen": manifest["supports_screen"],
            "supports_usb_help": manifest["supports_usb_help"],
            "modules": [{key: item[key] for key in ("id", "name", "version", "kind")}
                        for item in manifest["modules"] if item["kind"] == "native"],
        }
        guide = b"help"
        help_asset = {"file": "guide.txt", "size": len(guide), "path": "builder/help/guide.txt",
                      "sha256": hashlib.sha256(guide).hexdigest()}
        for item in manifest["modules"]:
            if item["kind"] == "sd":
                catalog["modules"].append(dict(item, asset=help_asset))
        update = update_manifest(ROOT, tag, commit)
        metadata = json.dumps({"catalog": catalog, "update": update})
        response = json.dumps({"tag_name": tag, "assets": [],
                               "body": f"Release notes\n\n{RELEASE_METADATA}{metadata}\n-->"}).encode()
        with patch("catalog.request", return_value=io.BytesIO(response)):
            self.assertEqual(refresh(self.root), catalog)
        with patch("program_update.request", return_value=io.BytesIO(response)):
            self.assertEqual(check_update(), update)
        with patch("catalog.request", return_value=io.BytesIO(archive.read_bytes())) as download:
            tree = source_tree(catalog, self.root)
        self.assertTrue(download.call_args.args[0].endswith("/zip/" + commit))
        self.assertEqual(load_modules(tree), manifest)
        with patch("catalog.request", return_value=io.BytesIO(guide)) as download:
            self.assertEqual(fetch_asset(catalog, help_asset, self.root).read_bytes(), guide)
        self.assertTrue(download.call_args.args[0].endswith(commit + "/builder/help/guide.txt"))

    def test_installed_package_remains_available_if_upgrade_copy_fails(self):
        card, cache = self.root / "card", self.root / "cache"
        card.mkdir()
        catalog = {"tag": "v0.2.0"}
        data = b"package contents"
        checksum = hashlib.sha256(data).hexdigest()
        module = {"id": "guide", "version": "0.1.0", "filename": "Controls.txt",
                  "asset": {"file": "guide.txt", "size": len(data), "sha256": checksum}}
        with patch("catalog.request", return_value=io.BytesIO(data)):
            self.assertTrue(install_sd(card, catalog, module, cache))
        upgrade = dict(module, version="0.2.0")
        with patch("catalog.shutil.copyfileobj", side_effect=OSError("Card removed")):
            with self.assertRaisesRegex(OSError, "Card removed"):
                install_sd(card, catalog, upgrade, cache)
        self.assertEqual(installed_version(card, module), "0.1.0")
        self.assertTrue(install_sd(card, catalog, upgrade, cache))
        self.assertEqual(installed_version(card, module), "0.2.0")

    def _flash_fixture(self, table_matches, fail_backup=False):
        directory = self.root / "build"
        directory.mkdir()
        table = partition_table()
        check_layout(table)
        identity = {"launcher": "0.2.0", "modules": {}}
        checksums = {}
        for name in ("firmware.bin", "partitions.bin", "bootloader.bin", "boot_app0.bin"):
            path = directory / name
            contents = {"partitions.bin": table, "boot_app0.bin": bytes(0x2000)}
            path.write_bytes(contents.get(name, b"firmware"))
            checksums[name] = digest(path)
        (directory / "build.json").write_text(json.dumps(dict(identity, sha256=checksums)), encoding="utf-8")
        history = []

        def esp(port, command, *args, after="no-reset"):
            history.append((command, args))
            if command == "read-flash":
                address, size, output = args
                if address == 0:
                    with output.open("wb") as stream:
                        stream.truncate(size)
            if command == "verify-flash" and args[0] == 0x8000 and not table_matches:
                raise RuntimeError("Verification failed (digest mismatch).")
            if fail_backup and command == "verify-flash" and args[0] == 0:
                raise RuntimeError("Backup did not verify")

        from contextlib import ExitStack
        stack = ExitStack()
        self.addCleanup(stack.close)
        stack.enter_context(patch("flash.find_download_port", return_value=("COM4", "mac")))
        stack.enter_context(patch("flash.identify", return_value="mac"))
        stack.enter_context(patch("flash.esptool", side_effect=esp))
        stack.enter_context(patch("flash.ports", return_value=[SimpleNamespace(device="COM4")]))
        stack.enter_context(patch("flash.comports", return_value=[]))
        stack.enter_context(patch("flash.read_identity", return_value=identity))
        return directory, history

    def test_routine_update_writes_application_only(self):
        directory, history = self._flash_fixture(True)
        self.assertEqual(flash("COM4", directory, report=lambda _: None), "mac")
        writes = [args for command, args in history if command == "write-flash"]
        self.assertEqual(writes, [("--flash-size", "8MB", 0x10000, directory / "firmware.bin")])

    def test_help_copy_creates_one_file_and_verifies_readback(self):
        from unittest.mock import MagicMock, call

        directory = self.root / "sdcard/Help"
        directory.mkdir(parents=True)
        data = b"Help\n"
        (directory / "test.txt").write_bytes(data)
        manifest = {"help_documents": ["test.txt"],
                    "sha256": {"sdcard/Help/test.txt": hashlib.sha256(data).hexdigest()}}
        serial = MagicMock()
        link = serial.return_value.__enter__.return_value
        link.read_until.side_effect = [
            b"CARDFILES HELP 1\n", b"OK\n", b"ERR File unavailable\n", b"OK\n",
            b"OK\n", b"OK 5\n", b"DATA " + data.hex().encode() + b"\n", b"END\n", b"OK\n",
        ]
        with patch("flash.serial.Serial", serial):
            self.assertEqual(copy_help_to_card("COM6", self.root, manifest), 1)
        remote = b"/Help/test.txt".hex().encode()
        self.assertEqual(link.write.call_args_list, [
            call(b"UF_HELP\n"), call(b"MKDIR 2f48656c70\n"), call(b"READ " + remote + b"\n"),
            call(b"PUT 5 " + remote + b"\n"), call(data), call(b"READ " + remote + b"\n"),
            call(b"NEXT\n"), call(b"NEXT\n"), call(b"DONE\n"),
        ])

    def test_flash_reports_help_copy_failure_separately(self):
        directory, history = self._flash_fixture(True)
        path = directory / "build.json"
        manifest = json.loads(path.read_text(encoding="utf-8"))
        manifest["help_documents"] = ["test.txt"]
        path.write_text(json.dumps(manifest), encoding="utf-8")
        status = []
        def unavailable(port, bundle, record):
            self.assertEqual(history[-1][0], "flash-id")
            raise ValueError("No microSD card is mounted")
        with patch("flash.copy_help_to_card", side_effect=unavailable):
            self.assertEqual(flash("COM4", directory, report=lambda _: None, help_result=status.append), "mac")
        self.assertIn("No microSD card is mounted", status[0])

    def test_routine_update_rejects_other_layout(self):
        directory, history = self._flash_fixture(False)
        with self.assertRaisesRegex(ValueError, "Partition table differs"):
            flash("COM4", directory, report=lambda _: None)
        self.assertFalse(any(command == "write-flash" for command, _ in history))

    def test_automatic_build_retention_preserves_current_fallback_and_named_exports(self):
        builds = [self.root / f"20261121-00000{index}-12345678" for index in range(4)]
        exported = self.root / "custom-recovery"
        for directory in [*builds, exported]:
            directory.mkdir()
            (directory / "build.json").write_text("{}", encoding="utf-8")
        trim_saved_builds(builds[1])
        self.assertEqual({x.name for x in self.root.iterdir() if x.name != ".pio"},
                         {builds[1].name, builds[3].name, exported.name})

    def test_compiler_prunes_scons_objects_to_the_budget(self):
        (self.root / "platformio.ini").write_text("[platformio]\n", encoding="utf-8")
        bucket = self.root / ".pio/buildcache/c9"
        bucket.mkdir(parents=True)
        old, recent = [bucket / ("c9" + digit * 28) for digit in ("a", "b")]
        old.write_bytes(b"old")
        recent.write_bytes(b"recent")
        import os
        os.utime(old, (1, 1))
        trim_compiler_objects(self.root, limit=6)
        self.assertFalse(old.exists())
        self.assertTrue(recent.exists())

    def test_migration_verifies_backup_before_writing_all_regions(self):
        directory, history = self._flash_fixture(False)
        backup = self.root / "original.bin"
        flash("COM4", directory, allow_migration=True, backup=backup, report=lambda _: None)
        commands = [command for command, _ in history]
        backup_check = history.index(("verify-flash", (0, backup.resolve())))
        self.assertLess(backup_check, commands.index("write-flash"))
        arguments = history[commands.index("write-flash")][1]
        self.assertEqual(arguments[2::2], (0, 0x8000, 0xE000, 0x10000))
        self.assertEqual(backup.stat().st_size, FLASH_BYTES)

    def test_migration_stops_on_backup_failure(self):
        directory, history = self._flash_fixture(False, fail_backup=True)
        backup = self.root / "original.bin"
        with self.assertRaisesRegex(RuntimeError, "Backup did not verify"):
            flash("COM4", directory, allow_migration=True, backup=backup, report=lambda _: None)
        self.assertFalse(backup.exists())
        self.assertFalse(any(command == "write-flash" for command, _ in history))

    def test_confirmed_migration_can_skip_full_flash_read(self):
        directory, history = self._flash_fixture(False)
        flash("COM4", directory, allow_migration=True, report=lambda _: None)
        reads = [args[0] for command, args in history if command == "read-flash"]
        self.assertEqual(reads, [])
        self.assertTrue(any(command == "write-flash" for command, _ in history))

    def test_declining_after_detection_skips_all_flash_writes(self):
        directory, history = self._flash_fixture(False)
        confirmations = []
        def decline(port, mac, migration):
            confirmations.append((port, mac, migration))
            return False
        self.assertIsNone(flash("COM4", directory, allow_migration=True, confirm=decline, report=lambda _: None))
        self.assertEqual(confirmations, [("COM4", "mac", True)])
        self.assertFalse(any(command == "write-flash" for command, _ in history))

    def test_mismatch_offers_migration_before_flash_confirmation(self):
        directory, history = self._flash_fixture(False)
        with patch("flash.read_identity", return_value={"launcher": "0.2.0", "modules": {}}):
            approvals = []
            def offer():
                approvals.append("offer")
                return True, None
            def confirm(port, mac, migration):
                self.assertTrue(migration)
                approvals.append("confirm")
                return True
            flash("COM4", directory, recommend_migration=offer, confirm=confirm, report=lambda _: None)
        self.assertEqual(approvals, ["offer", "confirm"])
        writes = [args for command, args in history if command == "write-flash"]
        self.assertEqual(writes[0][2::2], (0, 0x8000, 0xE000, 0x10000))

    def test_communication_error_is_not_a_layout_mismatch(self):
        directory, _ = self._flash_fixture(True)
        with patch("flash.esptool", side_effect=RuntimeError("Lost USB connection")) as esp:
            with self.assertRaisesRegex(RuntimeError, "Lost USB connection"):
                flash("COM4", directory, allow_migration=True, report=lambda _: None)
        esp.assert_called_once_with("COM4", "verify-flash", 0x8000, directory / "partitions.bin")
