# Cardputer ADV accessibility workspace

## Active project

- Run `git status` at the checkout root before editing and preserve existing changes.
- The desktop builder is in `builder/`. Unified firmware and tests are in `research/offline-reader/`. Use [README.md](README.md) as the documentation index.
- Follow [BUILDING.md](BUILDING.md) for builds, tests, module registration, help files, release packaging, and cleanup. Keep generated caches and private captures out of Git. Read upstream instructions before editing third-party checkouts.

## Maintaining accessibility

- Follow the [accessible app guide](ACCESSIBLE_APP_GUIDE.md) for apps and the [future firmware guide](FUTURE_FIRMWARE_GUIDE.md) for service ownership and lifecycle. For standalone firmware, use the [offline speech guide](TTS_FIRMWARE_GUIDE.md).
- Preserve keyboard-only use and offline speech. Reserve physical Alt for shared screen-reader and keyboard-layer commands. Check the [unified controls](research/offline-reader/UNIFIED_CONTROLS.md) before assigning app shortcuts.
- **Backspace is the global Back key in menus and navigation lists.** Verify it for every menu you add or change. See the [app guide](ACCESSIBLE_APP_GUIDE.md#2-describe-the-app-in-keyboard-and-speech-terms).
- Announce focus, state changes, and errors. Keep credentials out of speech and diagnostics when editing password flows. Distinguish host checks from live device and screen-reader acceptance.

## Building and flashing

- Follow [FLASHING.md](FLASHING.md). For a running launcher, do the 1200-baud touch before esptool. Match the new port and verify the MAC. Use esptool without an automatic reset in download mode.
- Routine updates write only the app at `0x10000` after an in-place partition digest check. Preserve the recovery image and skip flash dumps. Verify the written image and the reported version after reboot. Confirm a partition migration separately.

## Search and history

- For structural C++ or Python searches, prefer `ast-grep run --pattern "<syntax pattern>" --lang <language> <path>`. Use `rg` for text and filenames. Keep ast-grep local and on demand.
- For change history, use scoped `git log -S`, `git log -G`, `git blame`, or `git show` in the owning checkout first.
