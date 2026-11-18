# Offline Cardputer ADV reader/player test

Latest: [music plus speech and connected Wi-Fi](COEXISTENCE_RESULTS.md), followed by an [accessible CardEX community app with actual keyboard input](CARDEX_ACCESSIBILITY.md). Mixed audio is clear in the user's simulator listening check; CardEX interaction works, with brief pauses reported in its initial interactive build. Physical behavior and internet traffic are unverified. The CardEX guide includes the keyboard controls and simulator-only build instructions.

New [BackgroundAudio diagnostic](BACKGROUNDAUDIO_RESULTS.md): the user heard clear live English speech in Velxio for short/menu phrases, a difficult paragraph, Wi-Fi scanning, rapid replacement and stopping. Minimum free heap with Wi-Fi was 154,140 bytes; some buffer underruns remained. This isolated experiment has not replaced the SD apps and has not run on physical hardware or with connected network traffic.

Latest [Velxio audio results](VELXIO_RESULTS.md): actual I²S initialization works and prerecorded speech sounds clear, but live key-learning speech is unusably crackly and slow in the tested simulator. A diagnostic queue change did not fix it. The current SD package remains experimental, and physical performance is unverified.

[Audio startup investigation](AUDIO_RESEARCH.md) identifies an emulator I²S handshake limitation and a separate corrected DMA-idle setting. Physical speaker/headphone output remains unverified.

[Advanced pre-purchase test results](ADVANCED_TESTS.md) cover sustained emulator stress, installer fault injection, randomized HID input and audio-mixer checks, including incomplete Bluetooth and I²S startup probes. These diagnostic images are separate from the SD package.

The current headless package also includes an accessible Bluetooth/USB keyboard-and-mouse app and a separate key-learning app. Its speaking catalogue offers all three programs. Start with [keyboard/mouse and learner controls, SD layout and test evidence](HID_AND_LEARNING.md). `python build_package.py --headless` produces this package; the default build and original workflow below remain the earlier player-only variant.

The newer [headless Klatt variant](HEADLESS_RESULTS.md) removes graphics dependencies and replaces eSpeak with a smaller streaming backend. Its no-PSRAM core workload passed speech, MP3 and simulated Wi-Fi, including added memory pressure. Build its separate package with `python build_package.py --headless`; the earlier default graphical/eSpeak variant is retained. Hardware usability/audio remain unverified. The rest of this README describes the original default variant unless stated otherwise.

This is a pre-purchase software feasibility prototype. No physical device is needed to build or run the host tests. Compilation and simulations do not establish audible speech, acceptable latency, playback with speech/Wi-Fi, SD installation reliability, or physical keyboard usability.

## What it contains

- A dedicated speaking test launcher, built from the existing speech service and a small offline menu. It has no firmware catalog, downloader, WebUI or partition editor.
- A speech-enabled build of the actual MP3PlayerForM5Cardputer sketch, pinned to upstream `b7548630a222403424386f2d298043e360661d6a`. `prepare.py` checks the upstream file hash and applies guarded transformations in a generated build file; it never overwrites the upstream source.
- Shared semantic change detection and a centralized player-state adapter. This is not universal reading of arbitrary firmware or an implemented M5GFX text mirror.
- An SD package containing `accessible/media-player.bin`, the application image with its screen reader linked in. The image must be installed into internal flash before execution. Music files remain on SD.

The new project is independent of the previously modified full Launcher. It uses the normal framework bootloader and a new fixed layout: factory reader at `0x10000`, size `0x280000`; player OTA slot at `0x290000`, size `0x560000`; NVS, OTA metadata and coredump occupy the remaining regions. There is no general repartitioner or installer for arbitrary firmware.

## Build and package

From this directory in PowerShell:

```powershell
python build_package.py
```

This runs the media-player build first, then compiles that image's exact SHA-256 and length into the launcher. It verifies both generated partition tables, image fit, the merged image's embedded table/factory image, the compiled player digest and erased initial OTA selection. It stages `dist/sd-card`, creates `dist/initial-test-install.bin` and writes `offline-reader-test-package.zip`. No upload or physical-card copy is performed. Build logs are retained in `test-results`.

Dependencies include the sibling M5/eSpeak checkouts and the PlatformIO core under `../Launcher/.pio/core`. The first build downloads the stock Arduino framework libraries and ESP8266Audio 2.4.2. Version 1.9.7 failed against this Arduino framework's network API; the newer pinned release is used for this adaptation. The old custom keyboot override is intentionally absent from this project.

## Proposed device workflow, unverified

The initial merged image is only for a **dedicated test installation after full backup and a verified recovery method**. It replaces the flash layout and its write range includes settings/OTA metadata. It is not a data-preserving upgrade to an existing installation. Do not flash the previous Launcher layout together with these images. This project does not automatically flash any device.

1. Copy the contents of `dist/sd-card` to a FAT32 microSD. Add supported music files elsewhere on the card.
2. On the paired test installation, launcher semicolon/period moves among Run, Install and Help. Enter selects, R repeats, Space stops speech.
3. Install requires a second Enter. It validates the exact file hash before any player-slot erase, checks the paired layout and running factory image, writes only that player slot, validates the ESP image, and hashes the written bytes. It leaves the factory launcher and settings partitions intact. A failed write may destroy the previous test player; the launcher remains available for retry. Power-loss recovery is a design property still requiring hardware validation.
4. Run validates the installed image and selects the player for the next reboot. Player Control Q, followed by Enter, selects the paired factory launcher and restarts. Any other key cancels the return prompt. Cold-boot and return behavior have not been tested physically.

## Player behavior and controls

Normal player controls are retained: semicolon/period browse; Enter plays/pauses; N/B next/previous; brackets volume; S search; F shuffle; L loop; I help; backtick/Escape settings. Reader commands are intercepted before application handlers:

- Control F: speak current focus.
- Control T: playback status.
- Control R: repeat.
- Control Space: stop speech.
- Control H: reader help.
- Control Q: confirm return to test launcher.

Announcements use the existing player, settings, help, folder, search, Wi-Fi scan and text-entry states. Settings drawing and reading share their label generator. Selected filenames come from the full model, rather than shortened display text. Redraws are deduplicated; the observer runs independently of display sleep. Both passwords are masked and excluded from the reader snapshot/history. Typed password changes say only Character or Deleted. Actual key-chord ergonomics remain untested.

The visual startup animation/USB disk boot menu is omitted. Startup playback is paused after library loading so speech has a quiet starting point. The player uses a separate `reader_music` settings namespace and reader-prefixed cache/config files. No NVS erasure fallback is retained. The player may rescan its own cache and write its own settings through normal app actions.

Speech uses M5Unified virtual channel 1; music uses channel 0. Music gain is reduced while speech PCM is active. Codec initialization is shared; this does not prove smooth mixing, resampling quality, interruption response or sufficient runtime heap. Output rate starts at 44.1 kHz; bracket keys change music-channel gain without muting speech. Speech has a fixed starting gain. Voice is fixed English and speech requests are bounded to the existing service's 159-byte payload; long utterances can be truncated. Display capture, full document review, adjustable speech settings, rich priority scheduling and complete transient-error coverage remain future work.

Wi-Fi is off by default in a fresh player namespace but existing player Wi-Fi settings can be enabled. This player still contains its original optional local web streaming feature; the **launcher** has no network functionality. Scan, connect and playback under Wi-Fi load are not measured. The original app has synchronous scan/configuration paths that can delay key handling.

## Host tests

```powershell
python test/test_prepare.py
wsl --exec sh -lc "cd '/path/to/cardputer-accessibility/research/offline-reader' && g++ -std=c++17 -Wall -Wextra -Werror test/test_core.cpp -o /tmp/offline-reader-core && /tmp/offline-reader-core"
```

`python run_host_tests.py` runs the integration, core, actual player adapter, actual installer-transfer, headless board-glue and both speech-channel suites and retains their output. The adapter transcript is `test-results/player-adapter.txt`. The fakes supply model state, keys, board devices and output sinks; they do not run the decoder or render the actual player UI.

Run the existing `speech-prototype/test/test_speech_service.cpp` with its fake includes in both default and `-DSPEECH_OUTPUT_CHANNEL=1` configurations when changing shared output. These checks cover change detection, layout arithmetic, adaptation anchors, empty-library navigation guards, password masking and speech queue/channel selection. They do not emulate codec, SD/flash writes, bootloader, acoustic latency or radio coexistence. See the project status for exact observed results.

Upstream player license/credits are retained in its checkout and the package includes `PLAYER-LICENSE.txt` (GPLv3). The build recipe and transformations are in this workspace. Do not describe the packaged binaries as hardware-tested or as a complete firmware screen reader.
# Firmware authoring and new utility

See the [offline TTS firmware guide](../../TTS_FIRMWARE_GUIDE.md) and the new [Cardulator utility adaptation](../cardulator/ACCESSIBILITY.md). Cardulator has a separate simulator image and [runtime report](CARDULATOR_ACCESSIBILITY.md); its compact unit vocabulary and remaining startup crackle are documented. The user independently operated CardEX in Edge and reported that it worked pretty well after the later fixes. None of these results establish physical-device operation or replace the older SD catalogue images.

