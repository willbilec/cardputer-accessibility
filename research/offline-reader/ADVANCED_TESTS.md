# Advanced pre-purchase testing, 2026-11-04

Subsequent [audio research](AUDIO_RESEARCH.md) isolates the startup stall to an emulated I²S update handshake. The historical unmodified probes below remain incomplete. A separate run completes board initialization only after explicit debugger intervention; it is not an audio-output pass. A separate DMA auto-clear omission was corrected, and the headless package rebuilt; the diagnostic images remain outside its catalogue.

No Cardputer, physical SD card, USB host or Bluetooth peer was used. Diagnostic images are separate from the SD catalogue. Host tests and substituted-output emulator tests establish software properties under their stated models; they do not establish audible output, physical latency, radio reliability or recovery after real power loss.

## Host fault and stress tests

All **14 host suites pass** using `python run_host_tests.py`. Outputs are retained in `test-results`.

- The actual catalogue launcher's installer passes **58 success/fault cases**. Tests cover absent SD/files, wrong lengths/digests, zero/oversized images, layout/running-partition mismatch, seek/read errors, begin/end/boot failures, failures at all 19 modeled write boundaries, and modeled power loss at every boundary. Changing or truncating the SD image after preflight also prevents boot selection. Invalid preflight inputs cause no OTA begin; failed transfers abort; verification failures never reboot into the app.
- SD, flash, OTA and digest operations in these installer tests are explicit test doubles. This tests the real launcher's decisions and sequencing, not SHA-256 cryptography, ESP image validation, bootloader behavior or electrical power-loss recovery. Factory preservation in the model does not prove physical recovery.
- The actual HID event loop passes **30,000 deterministic randomized key transitions**, with periodic release sweeps. Menu/local-command states retain empty host reports. The run uses AddressSanitizer and UndefinedBehaviorSanitizer. Existing deterministic scenarios cover silent typing, mode changes, independent mouse-button release and release-safe confirmations.
- The actual audio mixer passes dual-channel 44.1/22.05 kHz mixing, stereo averaging, positive/negative clipping, cancellation of active audio and playback after cancellation. AddressSanitizer and UndefinedBehaviorSanitizer are enabled. I²S output and scheduling are fake; these are numerical/control-flow checks, not an acoustic-quality test.

## Emulator tests

Pinned ESP32-S3 emulator 0.51.0, no PSRAM. `emulation/advanced_tests.py` preserves input binaries/ELFs, merged-image hashes, commands, exit/timeout status, source hashes and checkpoints under `test-results/emulation`. A timeout with process exit zero is still **incomplete** unless the expected completion checkpoint was reached. Wall-clock time is only test-run duration.

### Sustained synthesis/music/Wi-Fi stress

`emulator-stress` uses the real Klatt speech service and libmad MP3 decoder, paced substituted audio outputs, the player's 16 KiB input buffer and an additional 12 KiB modeled PCM allocation. Requests occur every 200 emulated milliseconds, with stop every seventh action and repeat every eleventh remaining action. It runs 60 emulated seconds with Wi-Fi off, 60 with Wi-Fi enabled, and 60 after disabling Wi-Fi. A second case reserves another 65,536 bytes.

Both cases **completed successfully**, with no synthesis error, panic or decoder restart failure recorded. Each reached 899 actions, including 128 stops and 70 repeats, generated 2,589,184 speech samples and decoded 7,959,612 music frames. Wi-Fi connected during the middle phase and was disabled for the final phase. The image SHA-256 for both cases is `c06b29a23ca48f8d5ccef61a59ce1cd43affb060a0831531a60cc400d93a9faa`; reserve size differs through injected serial configuration.

| Measured property | Normal | Additional 65,536-byte reservation |
| --- | ---: | ---: |
| Emulated duration | 180 seconds | 180 seconds |
| Minimum free heap | 140,160 bytes | 70,528 bytes |
| Smallest observed largest free block | 110,580 bytes | 40,948 bytes |
| Free heap at end | 193,800 bytes | 124,168 bytes |
| Largest free block at end | 114,676 bytes | 45,044 bytes |
| Minimum reported speech-task stack headroom | 3,080 bytes | 3,080 bytes |
| First-PCM checkpoints | 771 | 771 |
| Emulated enqueue-to-PCM median / 95th percentile | 13 / 22 ms | 13 / 22 ms |
| Emulated enqueue-to-PCM minimum / maximum | 4 / 75 ms | 4 / 76 ms |

Free heap remained stable within the settled off, connected and final-off phases. **It did not fully return to its pre-Wi-Fi level**: the final total was 19,800 bytes below the warm pre-Wi-Fi value normally and 19,796 bytes below it under pressure. The largest block also remained smaller. This test cannot distinguish retained framework state from a loss that would accumulate across repeated Wi-Fi cycles; it is not proof of leak-free or day-long operation. Largest-block values are sampled observations, not a continuous minimum.

Raw outcomes are in `test-results/emulation/advanced-summary.json`; independently checked metrics and frozen-input hashes are in `test-results/emulation/advanced-validated.json`. Completion callbacks include cancelled generations; they must not be counted as fully spoken utterances. Per-generation first-PCM timing comes from the speech service itself. The original sequential benchmark's cumulative callback counters are not per-request timing in this stress mode.

Such timing excludes physical keys, codec, DMA and acoustic output and is not calibrated to hardware. This workload does not run the full player UI or real SD, keyboard or I²S drivers. It does not generate sustained Wi-Fi application traffic or establish long-term stability.

### Bluetooth initialization: incomplete

The probe runs the actual Arduino BLE/NimBLE initialization API alongside an initialized Klatt service, intending to advertise HID and speak twelve messages. It reaches `BLE_CHECKPOINT device_init` but does not return from `BLEDevice::init` before the emulator timeout. The emulator repeatedly reports HCI resets. Advertising and the twelve utterances were **not reached**. There is no successful Bluetooth/speech coexistence result or final Bluetooth memory budget. The probe's minimal HID descriptor is not the production report descriptor; no host protocol acceptance was tested.

### Full-board startup: narrowed, still incomplete

The probe invokes the actual headless keyboard/audio driver code, with generic register-file I²C devices substituted at 0x34 and 0x18. Wire and keyboard initialization complete; codec register writes, I²S channel creation and standard-mode setup return successfully. The final checkpoint is `BOARD_AUDIO after_i2s_std_mode`; `i2s_channel_enable` does not return within 15 emulated seconds. Neither `audio=1` nor the completion marker is reached. Generic register devices do not emulate a TCA8418 or ES8311's behavior. This isolates the stall but does not determine whether it is an emulator limitation or a problem that would also occur on hardware.

## Reproduction

Run these sequentially in `research/offline-reader`; do not run simultaneous PlatformIO builds in this project:

```powershell
python run_host_tests.py
python -m platformio run -e emulator-stress -e emulator-ble -e emulator-board-probe -j 6
python emulation/advanced_tests.py
python emulation/analyze_advanced.py
```

The advanced runner returns a nonzero status when a probe is incomplete; inspect the per-case summary rather than treating every timeout as success. The independent analyzer expects two completed stress cases and the recorded incomplete startup probes. It checks input hashes, reserve/PSRAM configuration, action/sample counts, Wi-Fi connection and error markers before deriving statistics. The runner explicitly generates diagnostic merged flash files with the app in the existing factory slot; these files are emulator inputs, not an installation recommendation. The runner never flashes a device. The shell-level WSL timeout bounds the Linux emulator process as well as the Python wait. Diagnostic images are not added to the SD catalogue; a later audio configuration fix is described in the linked research report.

## What remains unknown

Physical key-to-speech latency, voice quality through the ADV speaker/headphones, driver behavior, BLE pairing/security/reconnection, USB enumeration, real Wi-Fi/audio coexistence, sustained network throughput, battery use, installation and recovery remain unverified. These tests support continued prototyping; they do not establish a dependable accessibility product before purchase.
