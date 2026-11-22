# Cardputer ADV accessibility workspace

## Active project

- The workspace root is the GitHub checkout. Run `git status` here before editing and preserve existing changes.
- The desktop builder is in [builder](builder). The unified firmware, controls, and tests are in [research/offline-reader](research/offline-reader).
- Use [README.md](README.md) as the index, [BUILDING.md](BUILDING.md) for build, packaging, and test commands, and [FLASHING.md](FLASHING.md) for installation and recovery.
- When adding or changing a selectable module, follow [the module and help authoring guide](BUILDING.md#adding-modules-and-releasing). Register build inputs in `builder/modules.json`, put on-device help and setup resources in `builder/help/`, and update the unified controls when keys change.
- Keep tagged releases to the Windows builder and one universal firmware bundle. GitHub provides the source archives. Keep app resources in the tagged source and preserve the builder metadata in the release notes.
- The local `archived` directory is excluded from Git. It contains earlier experiments, old release artifacts, and private device captures. Preserve upstream checkout instructions and read them before editing third-party code.

## Maintaining accessibility

- Follow the [accessible app guide](ACCESSIBLE_APP_GUIDE.md) for apps and the [future firmware guide](FUTURE_FIRMWARE_GUIDE.md) for service ownership and lifecycle. For standalone firmware, use the [offline speech guide](TTS_FIRMWARE_GUIDE.md).
- Preserve keyboard-only use and offline speech. Reserve physical Alt for shared screen-reader commands. A tap of Alt switches keyboard layers in the Keyboard app, Key Map Manager, and NVDA Remote. Use unmodified keys or Option for app-specific commands, and check for existing shared Option shortcuts.
- Announce meaningful focus, state, errors, and progress. Avoid speaking newly entered passwords by default and keep credentials out of diagnostics. The legacy Wi-Fi password entry echoes keys, so account for that existing behavior if editing it.
- Use [unified controls](research/offline-reader/UNIFIED_CONTROLS.md) as the shortcut reference. The user uses a screen reader. Distinguish automated verification from live device or screen-reader acceptance.

## Building and flashing

- Build with the desktop builder or `python builder/build.py <new-output-directory> --preset all` from the checkout. It packages matching firmware, bootloader, and partition files with checksums. Run the builder and firmware host tests documented in BUILDING.md.
- For routine app-only updates, preserve the existing local recovery image and write only the application at `0x10000`. Skip partition backups and flash dumps. Check the installed table with esptool `verify-flash` against the build's `partitions.bin`. A layout migration is a separate confirmed operation.
- When the launcher is running, enter download mode before the first esptool connection. Open the application port at 1200 baud for about 250 ms, close it, then enumerate the ports again. The touch may disconnect the application port during the open. Windows may change the COM number, USB interface, and serial descriptor. Match the normalized serial or physical USB location and verify the MAC with esptool before writing. Use G0 and a data cable if no matching download port appears.
- Use esptool without an automatic reset when the device is already in download mode. Verify the partition digest before writing and the image digest afterward. Reboot and verify the reported version. Where listening is possible, check the spoken startup announcement.

## Search and history

- For structural C++ or Python searches, prefer `ast-grep run --pattern "<syntax pattern>" --lang <language> <path>`. Use `rg` for text and filenames. Keep ast-grep local and on demand.
- For change history, use scoped `git log -S`, `git log -G`, `git blame`, or `git show` in the owning checkout first.
