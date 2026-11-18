# M5Stack Cardputer ADV accessibility project

## Documentation index

- [Build an accessible app](ACCESSIBLE_APP_GUIDE.md) for the default agent
  workflow, including keyboard navigation, built-in speech and verification.
- [Design future accessible firmware](FUTURE_FIRMWARE_GUIDE.md) for shared
  services, memory, privacy, installation and verification.
- [Integrate offline speech](TTS_FIRMWARE_GUIDE.md) for BackgroundAudio,
  eSpeak, codec ownership, mixing and standalone ports.
- [Unified controls](research/offline-reader/UNIFIED_CONTROLS.md) for the
  current on-device controls and the USB serial SD browser.

The sections below record the project from the experimental Launcher,
calculator and timer through separate reader/player firmware, emulator
experiments, community app ports, physical diagnostics and the unified build.
Read dated evidence in order when an early report describes a condition
that changed in a later build.

## PlayPalace unified firmware, 2026-11-18

The development unit has the PlayPalace build and an expanded, single
7.875 MiB accessible application partition. Readback verified the app and
partition table. Checksums matched before and after the migration for the
bootloader, NVS settings, boot selector and core dump. The fresh identity
export was verified by SHA-256 at `/PlayPalace/identities-export.json`.

The physical unit connected to Wi-Fi, authenticated with the saved password
after token rejection, and navigated a live lobby. Crash dumps identified
allocation failures in eSpeak clause processing and the SD profile writer.
PlayPalace reserves the clause buffers and uses a smaller buffered SD writer
after the login packet is released.

| Physical sample | Free heap | Largest free block |
| --- | ---: | ---: |
| Earlier live navigation | 18–20 KB | about 8 KB |
| Revised build, Home with Wi-Fi off | 94,188 bytes | 51,188 bytes |
| Final run, Wi-Fi connected before WebSocket | 39,024 bytes | Not sampled at that transition |
| Final run, lobby and effects | 28,004 bytes | 16,372 bytes |
| Final run, twenty-item game menu | 19,868–22,432 bytes | 7,668 bytes at entry |

The final run reached a 4,484-byte heap low point during login. The TCP
worker retained 2,944 bytes of unused stack in the lobby and 2,672 bytes
at the sampled game menu. The largest frame received was 2,566 bytes.
PlatformIO's static RAM estimate fell from 164,356 to 157,780 bytes. The
firmware allocates the 4 KiB recorder/media/copy scratch buffer on demand,
uses 40 ms speech frames, and sizes the phoneme metadata for all 134 tables
in the shipped data. The fixed voice's unused 11 KB echo buffer was removed
in a preceding build. The voice, dictionary and waveform tables remain in
flash. An earlier game burst drained free heap to about 4 KB and a send
failed. The final firmware hands each frame directly to the UI task and
raises the heap reserve for optional sound downloads. Its monitored game
session stayed connected and produced mixer samples from the SD bank, with
zero network sound downloads.

The host suite and 247 PlayPalace Skippo and Golf server tests passed. The
435-effect SD sound bank was copied and verified by SHA-256. It covers common
effects, Skippo, Golf, Star Realms, Clank, and Space Base. Manual entry,
live table recovery, sustained
reconnect behavior, and first-play download caching still need physical
acceptance. See [unified controls](research/offline-reader/UNIFIED_CONTROLS.md).

## Unified firmware work in progress, 2026-11-17

The previous unified development firmware was installed in the physical ADV's
factory slot at `0x10000`. The verified application image is 2,590,032 bytes
and fits the installed 2.5 MiB slot. An earlier image built against a larger
partition failed to boot on the installed layout. The fitted app-only update
restored boot.

The user verified USB typing, Wi-Fi scanning, and an initial Bluetooth
connection. A Bluetooth startup test measured 103,756 bytes free before
startup and 30,696 bytes after. Windows saw the updated firmware's Bluetooth
advertisement. Android reconnection still needs acceptance. Windows USB mass
storage hung in testing. The replacement USB
serial browser listed the card and uploaded and downloaded a 4,050-byte test
file with identical content. Setting the clock from the PC also succeeded.
The full host suite passes. New recording gain, headphone speech quality,
three-slot Bluetooth switching, and sustained network and media workloads
still need device acceptance. See [unified controls](research/offline-reader/UNIFIED_CONTROLS.md)
and the [USB browser](research/offline-reader/usb_sd_browser.py).

M5Stack's official ADV-V0.3 factory demo and matching source are in
`research/M5Cardputer-UserDemo-ADV`. The official demo uses ESP-IDF 5.4.2
NimBLE, a public Bluetooth address, a 30–50 ms advertising interval, and
Secure Connections bonding without a PIN. The unified keyboard uses these
settings and lets the host initiate link security before issuing a fallback
security request. The app-only upload verified successfully. Windows saw a
connectable advertisement from this build, and Android pairing still failed
without a connection event in the device log. The downloaded factory binary
targets offset zero and was retained as a reference.

The user subsequently confirmed Windows pairing and reconnection after leaving
and reopening the keyboard app. Android's diagnostics showed several devices
named Cardputer ADV Keyboard. Failed attempts targeted entries ending in
`33:1C` and `93:20`, while Windows observed the live advertiser ending in
`FE:31`. Android selected `FE:31` and established an encrypted LE link.
The installed build gives each of three slots its own numbered name and
stable static random address. The four-character name suffix identifies the
Cardputer. The app replaces old shared-identity bonds on first Bluetooth
startup, so hosts need one fresh pairing. It filters saved-slot advertising
by the selected peer and maintains one active link at a time.
The user verified typing and switching between Windows on slot 1 and Android
on slot 2. A Windows BLE scan observed their distinct addresses ending in
`D1:CC` and `D1:CD`, and the Cardputer logged authentication on both switches.
Slot 3 has no paired-device acceptance test yet. The firmware build and
host suite passed.

## Physical device results, 2026-11-17

The user heard clear eSpeak speech on the physical ADV, including during a Wi-Fi scan. A diagnostic played a real stereo WAV from microSD with speech while connected to Wi-Fi and receiving HTTP responses; the user reported both smooth after a feed-rate fix. A physical-keyboard spoken menu worked. An observed multi-second menu delay improved after diagnostic serial output was made nonblocking, though longer testing is needed. Firmware uploads and restarts also succeeded automatically over USB. These are short diagnostic tests, not validation of the launcher, a general music player, other apps, battery life or long-term performance. See [physical results](research/offline-reader/PHYSICAL_RESULTS.md) for measurements, the incomplete original-flash backup and recovery details. Historical statements below that no device was connected describe earlier work.

The sections that follow describe earlier work. As of 2026-11-04 the
experimental launchers and test images compiled, and an ADV had yet to be
connected. The later physical trials above used specific diagnostic and
unified images. Installation and switching under the experimental Launcher
partition layout still lack physical acceptance.

## Latest: mixed audio, connected Wi-Fi and a community app

New utility work: the [firmware authoring guide](TTS_FIRMWARE_GUIDE.md) documents the tested BackgroundAudio integration and semantic announcements. [Cardulator](research/cardulator/ACCESSIBILITY.md), a separate community scientific calculator/scripting utility, now has an experimental spoken tools menu, mathematical input/results/errors, variables/constants, formula wizards, script output, settings and sampled plot data. Its dedicated headless simulator profile builds. The complete fallback unit catalogue caused a confirmed allocation failure/reset with speech, so this profile uses a documented compact catalogue. Its added keyboard/pronunciation and compact-unit host checks pass; the original 31-test suite retains one full-catalogue error-wording failure. Runtime/listening status is in the [utility report](research/offline-reader/CARDULATOR_ACCESSIBILITY.md). Startup crackle remains under investigation; do not describe this utility as fully accepted.

The [coexistence trial](research/offline-reader/COEXISTENCE_RESULTS.md) produced clear independent WAV music and live speech at normal pitch with the simulator reporting a connected Wi-Fi station and an IP address. Minimum free heap was 111,504 bytes, with zero reported output underruns in the final run. Outbound HTTP attempts failed in a prior image, so internet traffic remains unverified. Mixer byte-count and sample-rate bugs were corrected in the guarded dependency copy.

The actual [CardEX file manager/editor adaptation](research/offline-reader/CARDEX_ACCESSIBILITY.md) accepts desktop-keyboard input through the emulated keypad and speaks selection, editor lines, typing, undo, save, settings and recursive search. Early USB/display startup problems were bypassed in its explicit simulator profile. After buffer/chunk fixes, the user independently operated it in Edge and reported that it all seemed to work pretty well. Help reached its complete ending. A reported Stop delay included uncertain browser/control timing, so physical or end-to-end latency is not established. All 16 existing/extended host suites and a separate 56-key polling-reader sanitizer test pass. Physical operation remains unverified. These diagnostics have not replaced the SD catalogue binaries.

## Earlier: optimized BackgroundAudio trial

The isolated [BackgroundAudio/eSpeak-NG diagnostic](research/offline-reader/BACKGROUNDAUDIO_RESULTS.md), using changes found in OpenVocakey, produced clear live speech in the user's simulator listening tests: short and menu phrases, a difficult paragraph, Wi-Fi scanning, eight rapid replacements and explicit stopping. Minimum observed free heap with Wi-Fi enabled was 154,140 bytes. Buffer underruns still occurred, and Wi-Fi off did not recover all initial heap. This supersedes a blanket negative conclusion about simulator TTS, but does not validate physical hardware, connected network traffic or the complete applications. The SD catalogue still uses the earlier experimental engine.

## Earlier: Velxio Klatt audio trial

The [Velxio trial](research/offline-reader/VELXIO_RESULTS.md) runs actual I²S startup without the previous emulator workaround. The user heard the stock speaker example and clear prerecorded Klatt speech through our board driver in both large and small chunks. However, the actual key learner's **live speech was extremely slow, crackly and stuttering**; an experimental generation-ahead queue did not fix it. Treat this as a failed live-audio usability result in that simulator. It does not establish physical-device CPU/audio behavior. The earlier fake-output and core-emulator passes did not establish this property. No physical ADV has been tested; the existing SD package has not been replaced with the experimental variant.

## Advanced software testing

Subsequent [audio startup research](research/offline-reader/AUDIO_RESEARCH.md) traced the stall to the SDK polling an uncleared I²S update bit in emulator 0.51.0. Clearing only that emulated bit lets the original board firmware finish initialization. A separate SDK-only control stalls on both I²S ports, without our board or speech code. This strongly attributes the startup hang to emulator hardware behavior. A separate DMA-idle configuration omission was corrected to match M5Stack's automatic buffer clearing; host tests pass and the headless package is rebuilt. Physical speaker/headphone playback is still unverified.

The [advanced test report](research/offline-reader/ADVANCED_TESTS.md) records two successful three-minute Klatt/music/Wi-Fi emulator stress runs, 58 actual-installer control-flow success/fault cases, 30,000 randomized actual-HID-loop transitions and actual mixer numerical/cancellation tests. All 14 host suites pass, including sanitizer checks. Minimum free heap was 140,160 bytes normally and 70,528 with another 65,536 bytes reserved. Wi-Fi-off did not restore all pre-Wi-Fi heap; this is not a leak-free claim. The full-board startup stall is narrowed to `i2s_channel_enable`; the Bluetooth probe stalls during controller initialization. Both probes remain incomplete, and no physical Bluetooth/audio usability result has been established. Exact stress results, limitations and reproduction commands are in the report.

## Latest SD catalogue: keyboard/mouse and key learning

The headless package now contains three apps: the media player, a speaking adaptation of geo-tp's Bluetooth/USB keyboard and mouse, and a new key-learning program. The launcher speaks the catalogue and installs the selected exact image into the single application slot; all three images remain available on SD. The package is staged locally, with no physical card copy or flashing. See [controls and evidence](research/offline-reader/HID_AND_LEARNING.md).

The HID app has spoken connection/mode menus, local reader commands, connection status, and confirmed return. Ordinary typing stays silent to avoid exposing unknown remote password fields. The learner describes keys and modifiers without sending keyboard/mouse reports. All eleven host suites pass, including the real event loops with fake board/USB transports. All four firmware images build without graphics/eSpeak symbols; the learner has no linked HID transport. These checks do not establish physical pairing, enumeration, sound, runtime heap or latency. The older two-image results below describe previous milestones.

## Purpose and design

The project investigates an offline screen reader and application API for the M5Stack Cardputer ADV, along with an accessible launcher for switching among multiple firmware images. The ADV provides a keyboard, an ES8311 audio codec, speaker and headphone output, 8 MB flash and no PSRAM. Internal heap, audio scheduling and flash partitions are the main constraints.

Launcher is a separate firmware that installs and selects other firmware. Its speech implementation does not automatically make an arbitrary launched firmware accessible. The separate test firmware uses eSpeak NG and M5Unified and exposes a small C++ speech service for its own apps. MicroHydra is another separate MicroPython firmware; it would need its own speech integration and accessible apps.

## What has been built

### Smaller speech backend and graphics-free variant

The [headless Klatt experiment](research/offline-reader/HEADLESS_RESULTS.md) implements fixed-arena streaming speech and real player/launcher builds without M5Unified/M5GFX, fonts or sprite allocations. The Klatt core workload completed speech, MP3 and simulated Wi-Fi with no PSRAM: minimum free heap 141,908 bytes normally and 72,268 bytes with 65,536 extra bytes reserved. These are substituted-output emulator measurements, not complete headless-player/device measurements. Both headless images compile, and linked-symbol checks confirm graphics/eSpeak code is absent. A preview and separate test package are available; initial eSpeak failures and the original package remain preserved. Physical keyboard/audio glue, codec, SD/flash, switching and acoustic latency remain unverified.

### Emulator finding: eSpeak/MP3/Wi-Fi memory failure

The [ESP32-S3 emulator experiment](research/offline-reader/emulation/RESULTS.md) runs actual eSpeak and MP3 decoding with no PSRAM and substituted output hardware. Speech alone and speech/music completed, but adding simulated Wi-Fi reproducibly caused a 32,000-byte allocation to fail despite 64,988 bytes total free heap: the largest block was only 31,732 bytes. Allocating Wi-Fi/decoder first did not solve it. These are emulator observations, not physical-device measurements. The graphical player's omitted driver/buffer costs make the earlier compile-only result insufficient to establish memory feasibility. Smaller speech engines and a build excluding display code are now being investigated.

### Dedicated offline launcher and speaking media player

The user is evaluating feasibility before purchasing a device. `research/offline-reader` now builds an independent minimal speaking launcher (Run, Install from SD, Help) and a speech-enabled adaptation of the actual MP3PlayerForM5Cardputer sketch. Its prepared SD package contains the player image with the reader linked in; execution still requires installation to internal flash. The launcher has no downloader/catalog/WebUI and accepts only the packaged image's exact size/hash. Its new fixed factory/OTA layout uses the stock bootloader and is incompatible with the previous experimental full-Launcher layout. No device flashing or physical-card copy was performed.

The launcher binary is 1,721,904 bytes with 44,764 bytes static RAM; the player is 2,935,328 bytes with 77,124 bytes static RAM. Both fit the dedicated 8 MB layout. Both compile, packaging/layout/hash checks pass, and six host suites cover the integration, semantic observer, actual player adapter with fake state, installer transfer and default/channel-1 speech service. These do not establish runtime heap, device audio, latency, Wi-Fi/playback coexistence or physical boot/install/return behavior. See [results](research/offline-reader/RESULTS.md) and [build/controls](research/offline-reader/README.md). The prior full Launcher and upstream player checkout were preserved.

### Experimental accessible Launcher

The checkout at `research/Launcher` is based on bmorcelli/Launcher commit `e9d631e0cb2eaf49243fcff62235adac4f44fcb0`; the accessibility work is uncommitted. Its `m5stack-cardputer-adv-access` environment adds:

- ADV ES8311/I²S output and a bounded background eSpeak queue.
- Speech hooks for home and submenu selection, text-entry prompts and input, status banners, progress milestones and launch confirmation. Password entry announces a generic character rather than its contents.
- Entry into Launcher instead of a timed automatic app boot, and a 3 MiB experimental launcher partition.
- A desktop-only accessibility simulation target with a fake speech sink and scripted menu navigation.

This is not a complete screen reader. Speech settings, all navigation and status flows, robust message scheduling, installation, recovery and device behavior remain to be completed or tested. In particular, do not assume a successful build means the codec setup works.

The most recent ADV launcher build reported an application image of 2,483,824 bytes and approximately 102,844 bytes of static RAM. The merged `research/Launcher/Launcher-m5stack-cardputer-adv-access.bin` was checked to contain the current generated partition table. Static RAM numbers exclude runtime allocations. The ordinary `m5stack-cardputer` launcher also built successfully after the shared changes.

### Test firmware and community apps

`research/speech-prototype` has two different firmware targets: one starts in a basic two-operand calculator and the other in a timer. Both include the same demo menu, eSpeak service and network test. The timer is a C++ adaptation of the numeric-duration and seconds/minutes/hours controls of [foopod's MicroHydra timer](https://github.com/echo-lalia/MicroHydra-Apps/tree/main/app-source/timer), with a repeating spoken alarm. It is not the original Python app and has not been compared interactively on hardware. The calculator is a diagnostic app, not a full port of the community calculator.

The latest app-only images in `.pio/build/cardputer-adv-calculator/firmware.bin` and `.pio/build/cardputer-adv-timer/firmware.bin` were each 2,227,568 bytes and had different SHA-256 hashes. With the current layout and 64 KiB alignment, placing both would leave 655,360 bytes before app data and other flash requirements. This is arithmetic only: it does not verify the launcher's installer, partition creation, app booting or data preservation. Recalculate after changes.

`src/speech_service.h` exposes C++ calls to start the service, enqueue speech, stop, repeat and wait with a timeout. It is intended for the UI task, uses a bounded queue and fixed English voice/rate, and is not a MicroPython binding or accessibility tree. The test app also provides USB serial Wi-Fi connection and TCP load-test commands; see [its README](research/speech-prototype/README.md) for controls and the local test receiver.

The test apps have a guarded return-to-launcher shortcut. The inspected upstream `launcher_keyboot` source at `cf56f6a` selects the test partition after a deep-sleep wake under its default boot settings. The prototype checks for the paired `app0` partition and incompatible settings without changing them. A partition check cannot establish which bootloader is actually installed; real return and recovery remain untested.

## Verification completed

- The speech-enabled launcher and both test firmware targets built for ESP32-S3. These were cross-compiles, not executions on an ADV.
- The desktop launcher simulator, using Xvfb, fake audio and simulated input, announced the SD, Online Installer and Web User Interface menu items in order in five consecutive runs after the test waited for initial menu readiness. An earlier run missed an injected press before that readiness fix. This does not test hardware timing or the real codec.
- Host tests passed for calculator/timer logic, network-command handling with fake Wi-Fi and socket objects, the speech-service queue with fake synthesis/audio, and launcher-return decisions with fake NVS/partition/sleep functions. A local TCP receiver accepted 4,096 bytes on loopback.
- Simulations of the unmodified community calculator and the original timer countdown passed with fake MicroHydra hardware. Those Python apps themselves are not speech-enabled.

## Build and test commands

Run in PowerShell from the stated directories:

```powershell
# research/Launcher
python -m platformio run -e m5stack-cardputer-adv-access -j 6
python -m platformio run -e m5stack-cardputer-adv-access -t build-firmware -j 6
python -m platformio run -e m5stack-cardputer -j 6

# research/speech-prototype; run one PlatformIO process at a time here
python -m platformio run -e cardputer-adv-calculator -e cardputer-adv-timer -j 6

# workspace root
python research/test_calculator_host.py
python research/test_community_timer_host.py
```

For C++ host tests under WSL, use a C++17 compiler. The following examples assume this checkout's current Windows location; choose another output directory if the WSL cache is unavailable:

```bash
cd '/path/to/cardputer-accessibility/research/speech-prototype'
g++ -std=c++17 -Wall -Wextra -Werror test_models.cpp -o /tmp/cardputer-models
/tmp/cardputer-models
g++ -std=c++17 -Wall -Wextra -Werror -Itest/fakes test/test_network.cpp -o /tmp/cardputer-network
/tmp/cardputer-network
g++ -std=c++17 -Wall -Wextra -Werror -Itest/fakes test/test_speech_service.cpp -o /tmp/cardputer-speech
/tmp/cardputer-speech
g++ -std=c++17 -Wall -Wextra -Werror -Itest/fakes test/test_launcher_return.cpp -o /tmp/cardputer-return
/tmp/cardputer-return
```

The desktop simulator's `native-access-test` environment is in `research/Launcher/native/platformio.ini`. It needs Linux/WSL, PlatformIO and SDL2 development libraries; set `LAUNCHER_ACCESS_AUTOTEST=1` and use an X server such as Xvfb. The previous work installed temporary user-local dependencies under WSL's `~/.cache/cardputer-sim`; treat that cache as optional and verify it exists before relying on it. The test exits nonzero if its three expected menu announcements are not seen.

## Work still required

The [reusable screen-reader implementation plan](research/SCREEN_READER_PLAN.md) examines MP3PlayerForM5Cardputer at a pinned revision and proposes a common semantic reader, a small app-state adapter, optional M5GFX text review, and coordinated music/speech output. Launcher cannot keep its reader running across a separate firmware boot. This planning pass did not implement the reader, build the player or collect device measurements.

1. Finish launcher accessibility, including all input and status paths, speech controls, meaningful interruption/priority handling, and accessible recovery. Exercise more than the initial simulated menu route.
2. Validate the partition layout and launcher installation/switching paths with realistic image and data requirements. Verify safe recovery before flashing a device with existing data.
3. On an ADV, measure startup, key-to-first-audio latency, free/minimum/largest heap, task stack headroom, audio glitches and responsiveness with Wi-Fi off, connected, and transferring data. Test the speaker, headphones, cancellation, repeats and long sessions.
4. Test both app images and return-to-launcher behavior on the actual bootloader and device. Build a MicroHydra speech binding and adapt real community apps if feasible; assess their behavior with a screen reader user.
5. Update this document and `research/FEASIBILITY.md` with exact commands, results, limitations and dates as work proceeds.
