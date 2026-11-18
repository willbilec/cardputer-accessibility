# Pre-purchase software feasibility results

Date: 2026-11-03. The user does not own a device; this work assesses the software design before purchase. No hardware operation is claimed or required to reproduce these host/build checks.

Subsequent evidence: [ESP32-S3 emulation](emulation/RESULTS.md) reproduced an eSpeak contiguous-allocation failure when combining speech, MP3 and simulated Wi-Fi. This materially limits the positive build-only assessment below. The [smaller Klatt/headless variant](HEADLESS_RESULTS.md) subsequently completed the core emulator workload and produced graphics-free player/launcher builds. These are separate experimental variants, with physical-device behavior still unresolved.

## What was built

A dedicated offline speaking launcher and a speech-enabled build of the actual MP3PlayerForM5Cardputer program. The launcher contains Run, Install from SD and Help, with no network catalog/downloader, WebUI or partition editor. The player's reader observes its existing model/state machine instead of adding speech to every drawing statement. The original downloaded player source and the previous full Launcher accessibility changes were preserved.

The package is `offline-reader-test-package.zip`. Its `sd-card/accessible/media-player.bin` includes the reader; it is installed into the dedicated internal-flash player slot, rather than executed directly from SD. The launcher accepts only this build's player image after exact size/hash checks. Return uses the paired factory application and the normal ESP-IDF boot selection; this new project does not require the earlier custom keyboot bootloader.

## Build evidence

`python build_package.py` builds both environments sequentially. The compiler targets ESP32-S3, 8 MB flash, no PSRAM. Arduino framework 3.3.9 and its stock ESP32 libraries were used. The player uses upstream audio release tag 2.4.2 (commit `74fc1f09bbba5e5c5450b445452ba64ef2d8bbad`; its package metadata still reports 2.4.1). The older 1.9.7 release failed against the current framework's network API and was replaced.

| Image | Application binary bytes | Static RAM reported by linker | Assigned slot bytes | Unused slot bytes |
| --- | ---: | ---: | ---: | ---: |
| Offline speaking launcher | 1,721,904 | 44,764 | 2,621,440 | 899,536 |
| Speech-enabled media player | 2,935,328 | 77,124 | 5,636,096 | 2,700,768 |

Binary length includes image framing/padding, so it differs slightly from PlatformIO's program-use figure. The tool's default launcher percentage uses the OTA slot size; the package verifier checks the actual smaller factory slot explicitly.

Both generated partition tables match. The merged initial image embeds that table and the exact launcher binary at its factory address; its OTA-selection region is erased so the factory reader is selected initially. The compiled launcher contains the staged player's SHA-256. All partition regions fit inside 8 MB without overlap. `dist/manifest.json` records exact artifact hashes and lengths. The package builder checks this structure; it does not execute a bootloader.

These are static/link and packaging checks. Static RAM is not peak RAM consumption or guaranteed free heap. Speech allocates a 24,576-byte task stack; the player also allocates its 16,384-byte file buffer, 12,288-byte PCM triple buffer, decoder state, sprites and driver buffers. eSpeak and Wi-Fi add runtime allocations. Their simultaneous peak, allocation failures, largest free block and fragmentation are not measured here.

## Host evidence

`python run_host_tests.py` passed all six suites, using C++17 with `-Wall -Wextra -Werror` for C++ tests:

- Four Python integration tests exercised the real guarded transformation against the pinned upstream sketch, checked fail-closed anchors, protected-password masking, no NVS erase fallback, isolated cache/settings names, single keyboard update, shared settings descriptions and empty-library navigation guards.
- Semantic core tests checked initial screen/focus, 1,000 unchanged snapshots remaining quiet, focus/value/status changes, explicit repeat/current-focus behavior and rejection of wrong/overlapping/oversized layouts.
- The actual `player_access.h` adapter ran against fake model, keyboard, speaker and SDK objects for all seven UI states. It spoke full selected song names, playing state, settings, help, search, folder and Wi-Fi selection. Password content stayed out of captured speech; edits produced Character/Deleted. Stable selected/playing names did not repeatedly reread the playlist. Return required confirmation and refused a missing paired factory partition.
- The actual installer's transfer helper passed partial-read/exact-byte-copy, truncated input, flash-write failure, zero-size and oversized-read checks. These are byte-transfer simulations; SHA/image validation and actual flash erasure are not emulated.
- Existing speech-service fake-synthesis/audio tests passed with the unchanged default channel and with channel 1. Queue replacement, stop/repeat, waits, failures and selected output channel were checked.

The previous calculator/timer Python simulations and C++ model/network/return-policy tests were also rerun successfully. They remain tests of their original scope, not evidence that the new player has run on hardware.

Example output captured from the real player adapter with fake input/state:

```text
Music. First full song.mp3, 1 of 2
Second full song.mp3, 2 of 2
Settings. Wi-Fi Power: OFF, 6 of 19
Password. Input protected. Enter to save. Escape to cancel
Character
Deleted
Search. Query Second. 1 results. Second full song.mp3, 1 of 1
```

This is a text transcript, not synthesized audio or measured response time. Full host logs/transcript and successful compiler logs are retained in `test-results`.

## What this establishes for the purchase decision

The proposed two-image system can be compiled and packaged within the ADV's flash capacity, and a small centralized adapter can extract useful speech behavior from this existing application without narrating every drawing call. The offline launcher can be limited to our accessible build. Speech and music can be routed to distinct channels in the existing audio API, and the intended logic passes the stated simulations.

It does **not** establish that buying an ADV will immediately provide a dependable accessible music player. Runtime memory headroom, codec output, speaker/headphone quality, actual cancellation and audible latency, playback mixing/ducking, SD/flash installation, power-loss recovery, boot/return behavior, Wi-Fi coexistence, CPU underclocking and battery life remain unverified. The app retains synchronous operations and the reader retains fixed English speech, bounded utterances and incomplete transient-error/review coverage. See README.md for current controls and limits.

This is positive software feasibility evidence, with substantial hardware performance uncertainty. It is not yet evidence for arbitrary firmware becoming accessible or for treating the prototype as a finished accessible appliance.
