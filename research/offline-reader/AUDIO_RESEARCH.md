# Audio startup investigation, 2026-11-04

Later [Velxio testing](VELXIO_RESULTS.md) independently runs the SDK probe past I²S enable and produces clear prerecorded speech through our driver. However, live synthesis is badly stuttering and slow in that browser runtime. Initialization success is therefore not a usable-speech result; the later report contains the listening evidence.

**The previously observed startup stall is strongly attributable to the emulator's missing I²S hardware handshake.** The debugger found the exact wait, and supplying that one missing handshake let the unchanged board firmware finish audio initialization. Separately, the lightweight driver omitted DMA automatic clearing; that configuration issue has been corrected. Neither finding establishes audible speech through a physical speaker or headphone jack.

No Cardputer or other physical audio device was used. No firmware was flashed. Emulator-only register intervention is a diagnostic experiment, not a production workaround.

## What the debugger established

The original advanced-test board image is preserved at `test-results/emulation/advanced-images/board-checkpoints`, with merged-image SHA-256 `ff8cf094459ca52e98ea3664a07cc619eece622c524c221f0854daa8759c45e3`. It uses the pinned ESP32-S3 emulator 0.51.0 without PSRAM and generic I²C register devices for the keyboard and codec.

An unmodified run stalls after standard I²S configuration. GDB inspected both emulated CPUs: CPU 0 was idle; CPU 1 was executing the SDK's `i2s_ll_tx_update` at address `0x42014653`/`0x42014655`. The call chain was:

```text
setup -> HeadlessSpeaker::begin -> i2s_channel_enable
      -> i2s_tx_channel_start -> i2s_ll_tx_start -> i2s_ll_tx_update
```

The driver sets the peripheral's update bit and polls until hardware clears it. This is the SDK's normal [ESP32-S3 I²S startup handshake](https://github.com/espressif/esp-idf/blob/v5.5.4/components/hal/esp32s3/include/hal/i2s_ll.h#L382-L408), reached through [channel startup](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2s/i2s_common.c#L110-L129). The installed SDK header has the same loop at lines 408–411. The debugger observed `I2S1.tx_conf.tx_update = 1`, with the peripheral at `0x6002d000`; the instruction was testing bit 8 and branching back.

In a separate run, GDB changed only the **emulated** `I2S1.tx_conf.tx_update` field to zero. The firmware image, codec initialization, driver and pins were unchanged. The log then reached:

```text
BOARD_AUDIO after_i2s_enable
BOARD_PROBE audio=1
BOARD_PROBE_COMPLETE
```

The observed register then had `tx_update=0` and `tx_start=1`. This causally isolates the blocking condition. It does not model I²S DMA progression, validate generated clock signals or turn this into an audio-output test. Diagnostic disassembly also produced two emulator unmapped-read warnings; those are recorded in the raw log and are not hidden as firmware failures.

Evidence: `test-results/emulation/audio-gdb.txt`, `audio-gdb-emulator.log`, `audio-gdb-clear-update.txt`, `audio-gdb-clear-update-emulator.log` and their command/run metadata. The untouched run still stalls; only the explicitly intervened run reaches completion.

## What the emulator documents

The [pinned release README](https://github.com/espressif/esp-emulator/blob/v0.51.0/README.md#features) lists modeled peripherals including GDMA and I²C, but does not list I²S. Its public repository distributes documentation, binaries and test utilities; it does not expose the Rust peripheral implementation for inspection. An [open feature request](https://github.com/espressif/esp-emulator/issues/5) proposes an I²S PCM bridge, including codec control through I²C. That request is supporting context, not a maintainer guarantee about every register.

The decisive evidence here is the observed hardware-update wait and the controlled register intervention, rather than absence from a feature list alone. I would not bypass that wait in device firmware: real hardware is supposed to acknowledge configuration itself.

## Comparison with M5Stack's driver

An independent **SDK-only control** also reproduced the stall on both I²S ports 0 and 1. It contains no speech synthesis, codec initialization, keyboard driver, headless board implementation or display library. Channel creation and standard-mode configuration returned success on each port, followed by a timeout inside channel enable. Each was allowed ten emulated seconds. Raw logs and frozen inputs are under `test-results/emulation/i2s-sdk-control*`; the summary is `i2s-sdk-control-summary.json`. This further separates the startup failure from our application and ES8311 initialization code.

The vendor checkout is unmodified at `44952cb8e4ac88bf04ad23b9dfd6f4b05c788348`. Our eight ES8311 initialization writes match the vendor's [Cardputer ADV speaker callback](https://github.com/m5stack/M5Unified/blob/44952cb8e4ac88bf04ad23b9dfd6f4b05c788348/src/M5Unified.inl#L1045-L1067), including BCLK-derived codec clocking, DAC power-up and headphone-drive enable. I²C address 0x18 and SDA/SCL 8/9 agree with that code and the board wiring. The vendor selects I²S port 1, BCLK 41, WS 43 and data out 42; our driver uses the same pins and 16-bit Philips framing.

The board's [official documentation](https://docs.m5stack.com/en/core/Cardputer-Adv) confirms the ES8311, onboard speaker and 3.5 mm output, and says inserting headphones disables the speaker amplifier. This supports the design's plausibility for both outputs; it does not certify this firmware. The minimal driver still differs from M5Unified's full mixer, clock-management and lifecycle implementation.

## Separate code issue corrected

M5Stack's [I²S setup](https://github.com/m5stack/M5Unified/blob/44952cb8e4ac88bf04ad23b9dfd6f4b05c788348/src/utility/Speaker_Class.inl#L182-L190) enables `auto_clear`. Our driver had inherited the SDK default, which leaves that flag false. According to the [SDK configuration definition](https://github.com/espressif/esp-idf/blob/v5.5.4/components/esp_driver_i2s/include/driver/i2s_common.h#L20-L76), enabling it clears transmitted DMA buffers so the output sends zero when fresh data is unavailable.

Because our mixer waits when idle, leaving this disabled risks stale audio being retransmitted during gaps or underruns. `HeadlessSpeaker::begin` now sets `channel.auto_clear = true`; the board-glue test checks the requested policy. This aligns idle behavior with the vendor and addresses a genuine configuration omission. The fake I²S tests cannot establish the physical symptom or its audible severity. This setting does not make the emulator implement its update handshake.

All fourteen host suites pass after the correction, including the mixer and HID sanitizer runs. The headless package is rebuilt so its apps share the corrected DMA policy. Exact image sizes and hashes are recorded in its manifest; the preserved debugger input remains the original image above.

## Reproduction

The debugger experiments use the preserved original image, so later package rebuilds do not change their input:

```powershell
python emulation/debug_audio_startup.py
python emulation/debug_audio_startup.py --clear-update
```

The second command modifies only emulated registers. Raw GDB commands and logs remain under `test-results/emulation`. For the SDK-only control:

```powershell
python -m platformio run -e emulator-i2s-control -j 6
python emulation/test_i2s_control.py
python emulation/verify_audio_research.py
```

Do not run simultaneous PlatformIO builds in this project. Diagnostic images are not in the SD catalogue. The normal headless package build and host tests remain `python build_package.py --headless` and `python run_host_tests.py`.

## What this changes about the feasibility assessment

The startup hang is no longer an unexplained symptom that weighs equally against hardware and emulator: we have direct evidence of an emulator handshake limitation. The hardware configuration also has a vendor-supported basis, and the idle-DMA omission has been corrected. Confidence that offline speech can be made to work on the ADV is therefore higher.

Physical audibility, speaker/headphone levels, DMA playback, interruption timing, underrun behavior and Bluetooth/audio coexistence still require real-device acceptance. A completed initialization after debugger intervention is not proof of audible output, and no device purchase can be justified as a guarantee of a finished accessible product.
