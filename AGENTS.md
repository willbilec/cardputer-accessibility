# Cardputer ADV accessibility project

## Goal and current state

Continue the Cardputer ADV feasibility project described in [PROJECT_STATUS.md](PROJECT_STATUS.md): an accessible multi-firmware launcher, a reusable offline speech interface, and accessible test applications, including community applications. The user wants evidence about responsiveness and Wi-Fi on and off, not only a successful build.

This is an experimental prototype. Later work includes short physical ADV trials and a unified development image. Use [PROJECT_STATUS.md](PROJECT_STATUS.md) for the device results and open acceptance work. Keep builds, simulated tests and physical listening results distinct. Check the actual installed layout and protect existing device data before installing an image.

## Authoring accessible apps and firmware

- For any Cardputer ADV app request, follow the [accessible app implementation guide](ACCESSIBLE_APP_GUIDE.md) automatically. Include keyboard-only operation and built-in offline TTS even if the request only describes the app's features. Use the shared unified firmware reader for built-in apps and integrate a reader into standalone firmware.
- Follow the [future firmware guide](FUTURE_FIRMWARE_GUIDE.md) for shared service ownership, app lifecycle, privacy, installation and physical acceptance. Use the [offline speech integration guide](TTS_FIRMWARE_GUIDE.md) for standalone firmware and ports.
- Use [unified controls](research/offline-reader/UNIFIED_CONTROLS.md) for the current shortcuts and [project status](PROJECT_STATUS.md) for the work and evidence already recorded.

## Where things live

- `research/offline-reader/`: unified firmware and earlier offline launcher, player, HID and speech experiments. Build `unified` or `unified-dev` there and use `python run_host_tests.py` for its host suite.
- `research/Launcher/`: optional upstream bmorcelli Launcher checkout. The archived experimental ADV speech changes are under `research/patches/`. Read Launcher's own `AGENTS.md` and `CLAUDE.md` before changing it.
- `research/speech-prototype/`: C++ eSpeak service, calculator and timer-adaptation firmware, Wi-Fi load test, host tests, and its own README. Its PlatformIO configuration references sibling dependency checkouts and `research/Launcher/.pio/core`.
- `research/MicroHydra/` and `research/MicroHydra-Apps/`: upstream MicroPython firmware and community apps. The original apps are not yet speech-enabled. The prototype timer is a C++ adaptation, not the original Python app.
- `research/FEASIBILITY.md`: technical evidence and outstanding device tests. Keep it consistent with source and test results.
- `research/myLibBuilder-keyboot/`: the upstream custom bootloader source examined for return-to-launcher behavior; actual installed bootloader behavior remains unverified.

Run `git status` in the project or relevant upstream checkout before editing. Downloaded source checkouts are ignored by the root Git repository. Record changes to historical adaptations under `research/patches/`.

## Search and verification

- For routine app-only firmware updates to the Cardputer ADV, use the existing firmware image as the rollback source and skip flash backups. Verify the connected device and its installed partition layout, then write only the application partition. Follow any explicit user request for a backup.

See [Flashing and recovery](FLASHING.md) for the public installation procedure.

- For syntax-aware code searches, use `ast-grep run --pattern "<syntax pattern>" --lang <language> <path>` before text grep. Use `rg` for ordinary text, file names, logs and configuration. Do not add a persistent ast-grep MCP wrapper.
- When a task concerns when, why or by whom code changed, use scoped local Git history first: `git log -S`, `git log -G`, `git blame`, `git show` and `git diff` as appropriate.
- Build the speech launcher with `python -m platformio run -e m5stack-cardputer-adv-access -j 6` in `research/Launcher`. Use `-t build-firmware` to force regeneration of the merged binary after partition-table changes; verify its embedded partition table matches the generated one. Also test `m5stack-cardputer` when changing shared launcher code.
- Build both app images with `python -m platformio run -e cardputer-adv-calculator -e cardputer-adv-timer -j 6` in `research/speech-prototype`. Do not run simultaneous PlatformIO builds in that project: they have previously interfered with build artifacts.
- Run the host tests described in `PROJECT_STATUS.md`. Host fakes deliberately do not emulate physical codec, keyboard, Wi-Fi, flash, bootloader, or timing.
- Treat runtime measurements as real only when collected on the target under the stated conditions. Verify the actual running device and connected serial port rather than assuming one is present.

## Accessibility and app design

- Use Alt for app-specific commands, help, and settings adjustments. Keep Option for shared reader controls and performance modifiers that do not change app settings. Preserve existing global shortcuts.

- Preserve keyboard operation and announce semantic selection, state, input, errors, and progress; do not infer focus or reading order from drawing text alone. Avoid interrupting critical messages with excessive redraw or progress announcements.
- Keep password characters silent by default in new flows and keep credentials out of diagnostics. The unified Wi-Fi screen currently echoes typed characters while redacting them in diagnostics. Account for that existing behavior explicitly when changing the flow.
- The prototype's `speech_service.h` is a small C++ UI-task API, not a MicroPython binding or full screen-reader accessibility tree. Account for its bounded queue, interruption behavior and fixed voice; test changes with its fake-audio tests and later with actual audio.
- Launcher and installed firmware are separate programs; each needs its own accessibility support. Check partition sizes and bootloader behavior against the actual image and hardware instead of assuming that all catalog firmware can coexist or return to Launcher.
- The user uses a screen reader. Prefer text-first documentation and keyboard-accessible flows; distinguish automated checks from live screen-reader and physical-device acceptance.
