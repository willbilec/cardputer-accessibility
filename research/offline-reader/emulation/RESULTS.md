# ESP32-S3 emulation: initial eSpeak results

2026-11-03. Tested with Espressif `esp-emu` 0.51.0, native Linux x86-64 binary under WSL, ESP32-S3 with **zero PSRAM**. The release archive's SHA-256 was verified. Commands, image hashes, logs and extracted results are in `../test-results/emulation`.

Follow-on results: the [smaller Klatt/headless variant](../HEADLESS_RESULTS.md) completed the comparable core workload, including added memory pressure. The eSpeak findings below remain evidence about that backend and configuration, not every possible screen-reader implementation.

The original offline launcher image boots, initializes actual eSpeak and reports 236,440 bytes free heap. It reaches first PCM submission at 9 emulator milliseconds but does not complete speech within 10 emulator seconds. The keyboard expander is not present, and audio output does not drain. This is not an end-to-end successful launcher run.

A separate target, `emulator-workload`, therefore runs the actual shared eSpeak service and the same libmad MP3 decoder as the player with a generated 3-second, 128 kbps stereo 44.1 kHz MP3 in flash. It preserves the player's 16,384-byte source buffer and reserves its 12,288-byte PCM triple-buffer allocation. Output is consumed by paced test sinks; display, keyboard, physical SD, codec, mixer and DMA drivers are not exercised. The workload's static RAM is 65,940 bytes, versus 77,124 for the actual graphical player, and it also omits display/driver runtime allocations. It is an optimistic core workload, not a full measurement of the physical player's RAM.

## Observed initial run

| Stage | Outcome | Minimum free heap by end of stage | Time to first speech PCM |
| --- | --- | ---: | ---: |
| Speech only | Completed 60,954 real samples | 134,088 bytes | 9 emulator ms |
| Speech plus paced MP3 decoding | Completed 68,177 real speech samples | 87,304 bytes | 2 emulator ms |
| Speech plus MP3 plus connected simulated Wi-Fi | Firmware aborted | Last checkpoint minimum: 62,996 bytes | Failed before first PCM for this request |

At failure eSpeak requested a **32,000-byte contiguous buffer**. Internal free heap was 64,988 bytes, but its largest free block was only 31,732 bytes; PSRAM free was zero. The real allocator reported failure and the firmware asserted at `MakePhonemeList`, `phonemelist.c:154`. `N_PHONEME_LIST` is 1000 and the library allocates this scratch array for phoneme processing. Total free heap alone is insufficient to characterize this failure.

The exact same merged image (`f271e07753b3ac97922473fbf5b6a2634d56a36a96028e8987ac3c81284df605`) reproduced the same failure and first-PCM readings on a fresh emulator run. Reserving an additional 65,536 bytes caused the same allocation failure earlier, during music plus speech before Wi-Fi started. That reserve is a stress test, not a measured estimate of the omitted drivers.

An allocation-order experiment initialized Wi-Fi and MP3 before speech. Wi-Fi connected, and some PCM was produced, but the first utterance still aborted on the same 32,000-byte request, with 64,860 bytes free and a 31,732-byte largest block. Merely changing startup order did not solve it.

The initial failing image and source were preserved as `initial-workload.bin` and `initial-workload.cpp.txt` in the log directory. The ordered experiment has a different image hash recorded with its run. The analyzer preserves firmware aborts as failures; emulator timeout/process termination alone is not a passed application test.

## Interpretation

This is a reproducible firmware memory-fragmentation failure **under this emulator and workload**, not proof of identical physical behavior. It is a substantial warning for a no-PSRAM design, especially because the full graphical player needs additional resources. It changes the pre-purchase assessment: the current eSpeak/music/Wi-Fi combination should not be considered ready or adequately provisioned based on successful compilation.

The 9/2 ms figures are synthetic request-to-first-generated-PCM measurements in the emulator. They exclude physical keyboard scanning, reader polling, codec/DMA buffering and acoustic onset. The emulator has not been calibrated for ESP32-S3 cycle/cache/radio timing. The output sink deliberately adds modeled playback delays, so utterance duration is not a pure CPU benchmark either. There is one request per initial condition, with an exact deterministic repeat; no p95 or hardware latency claim is supported.

Next investigation: compare a smaller streaming formant synthesizer and remove display code/buffers at compile time. Turning off the LCD backlight alone does not free those allocations. Preserve this failing baseline while evaluating alternatives.

## Reproduction

From `research/offline-reader`:

```powershell
python emulation/run_emulator.py
python emulation/prepare_fixture.py
python emulation/test_workload.py
python emulation/test_order.py
python emulation/analyze_results.py
```

The failing workload/order tests return failure until the underlying issue is resolved. The initial exact image can be replayed from the retained binary; subsequent workload source revisions produce different hashes. The original player/launcher package has not been replaced by this emulator-only firmware. Six existing host test suites passed again after adding the opt-in emulator sink.

Sources: [Espressif emulator release](https://github.com/espressif/esp-emulator/releases/tag/v0.51.0), [emulator CLI and memory configuration](https://github.com/espressif/esp-emulator/blob/main/docs/guides/CLI.md). Its support for running a target binary does not establish a complete Cardputer or cycle-accurate audio model.
