# Smaller synthesizer and a genuinely headless player

2026-11-03. **Both changes are implemented as experimental build variants.** Streaming Klatt completed the no-PSRAM speech/MP3/simulated-Wi-Fi workload that crashed eSpeak, including a run with 65,536 additional bytes reserved. The actual player and paired launcher compile without graphics dependencies. No device is owned or connected; these remain software and emulator results.

## Engines considered

| Engine | Evidence and decision |
| --- | --- |
| Existing eSpeak NG | Repeatedly aborted on a 32,000-byte contiguous allocation with MP3 and simulated Wi-Fi. Retained as the failing baseline. |
| ESP8266SAM | Available locally at `bb3e2559387dc89b43cc1a6c37abaa02eb8410d1`; targets smaller ESP8266/ESP32 systems. Upstream describes rougher formant speech. Kept as an alternative, not selected for these builds. |
| Moonshine streaming Klatt | Selected and tested. C++17, caller-owned arena, streaming PCM without a complete-utterance waveform allocation. Sparse checkout pinned to `234f60faa0eb388b01cdf7e60aca232af37aefda`. Core is MIT licensed; the baked dictionary derives pronunciations from CMUdict. |
| PicoTTS / ESP32 Flite ports | Not selected for this no-PSRAM target. Adafruit documents about 2.5 MB RAM for PicoTTS; the inspected ESP32 Flite port requires PSRAM. |

Sources: [Klatt core](https://github.com/moonshine-ai/moonshine/tree/234f60faa0eb388b01cdf7e60aca232af37aefda/micro/klatt-tts), [Adafruit's comparison](https://learn.adafruit.com/circuitpython-text-to-speech/overview), [ESP8266SAM](https://github.com/earlephilhower/ESP8266SAM), [ESP32 Flite](https://github.com/alkhimey/esp32-flite). Documentation guided the selection; measured values below come from this project's runs.

## Implemented speech backend

`src/klatt_speech.cpp` preserves the speech API, bounded pending request, replacement/cancellation and repeat behavior. It uses an 8,192-byte worker stack and a fixed 32,768-byte arena. The shared `include/klatt_render.h` breaks longer requests into bounded fragments and retries smaller fragments when the arena is insufficient. The front end still makes transient allocations; this is not an allocation-free service. It renders 256 samples at a time and never retains a complete utterance's audio.

The voice is English formant speech, not a neural/natural voice, producing mono 22,050 Hz PCM. The host-generated `test-results/klatt-sample.wav` uses the same renderer and voice settings. Its 8.47-second clip says Music, Down selects next song, Enter plays or pauses, Wi-Fi connected. Listening quality and pronunciation should be judged from the sample; tests establish finite nonempty PCM, not intelligibility.

## Emulator comparison

Espressif `esp-emu` 0.51.0 runs actual ESP32-S3 binaries with **zero PSRAM**. The shared workload uses the player's real libmad decoder, its 16,384-byte source-buffer size, a 12,288-byte PCM-buffer reservation, the same generated 128 kbps stereo MP3, paced output sinks and framework Wi-Fi against the emulator's access point. Graphics, keyboard, physical SD, codec, mixer and DMA paths are substituted. The physical headless audio driver is separately compiled; these sink-based tests do not validate it.

| Core workload | eSpeak result | Klatt result | Klatt minimum free heap |
| --- | --- | --- | ---: |
| Speech only | Completed | Completed | 245,156 bytes |
| Speech plus MP3 | Completed; minimum 87,304 bytes free | Completed | 197,584 bytes |
| Speech plus MP3 plus connected simulated Wi-Fi | Aborted on 32,000-byte allocation | Completed | 141,908 bytes |
| Same sequence with 65,536 extra bytes reserved | Aborted when music/speech began, before Wi-Fi | Completed all three utterances | 72,268 bytes |

At the end of the normal Klatt run, free heap was 157,588 bytes and the largest block 114,676 bytes. Under pressure, those values were 87,948 and 45,044 bytes. The reserve is a controlled stress test, **not a measurement of omitted drivers**; serial-input setup can also affect allocations.

Each Klatt run completed three utterances, generated 368,610 speech samples and decoded 776,581 music frames with a nonzero checksum. Wi-Fi connected. The speech task retained 3,304 bytes of stack headroom in these cases. No active Wi-Fi transfer, long-session test or file-format matrix was exercised.

First generated PCM was reported at **8 emulator milliseconds** for each request. This synthetic enqueue-to-PCM checkpoint excludes physical key handling, reader polling and acoustic output. Instruction/cache/radio timing is not calibrated to hardware. Paced sinks deliberately add playback delays, and speaking rates/utterance lengths differ between engines; utterance duration is not a controlled CPU-speed comparison. No hardware latency or percentile is established.

Full logs, commands, exit codes and hashes are in `test-results/emulation`; `summary.json` contains extracted checkpoints. The tested Klatt workload hash is `670135f2ff5dd26d7eaae04fa78a606db54c1c11aad39619569da2926a9444c5`. Both normal and pressure cases exited successfully on `BENCH_COMPLETE`. Original eSpeak aborts remain recorded as failures.

The subsequent [advanced test report](ADVANCED_TESTS.md) contains sustained speech-interruption stress and more precise startup probes. Its diagnostic images are separate from the packaged firmware and the original core workload above. The later [Velxio audio trial](VELXIO_RESULTS.md) found clear prerecorded output but unusably crackly/slow live speech; the successful core workload above did not establish audible live performance.

## What headless removes

The actual player retains its state machine, library, search, settings, Wi-Fi and playback logic. Drawing bodies, the visual startup/USB menu, fonts and graphics dependencies are removed from generated source. Sprite/framebuffer allocations cannot occur. Small no-output compatibility methods let status-print calls compile without a graphics driver. Backlight GPIO 38 is held low and LCD chip-select inactive; no LCD initialization occurs.

The graphical player's normal sprite would allocate 9,912 bytes and its optional full-screen sprite 64,800 bytes. Those allocations are absent here. This does not mean every run saves their sum: the optional sprite is not always allocated, and the replacement speech/audio components have costs of their own.

Minimal board glue polls the ADV TCA8418 on I²C pins 8/9 and uses ES8311/I²S settings from inspected M5Stack sources. A single output task owns two bounded channels and mixes music/speech at 44.1 kHz with simple rate conversion. Physical sound quality, DMA behavior, keyboard/button timing, mixing and cancellation remain unverified. Audio write faults are logged; successful enqueue is not proof of audible playback.

Display-only settings are omitted from keyboard navigation. Reader shortcuts, protected input and full selected labels remain available. The headless player has a separate `headless_music` NVS namespace, fixed output rate and paused startup. Necessary keyboard, SD and audio drivers are retained.

The paired launcher now lists the media player, Bluetooth/USB keyboard and mouse, key learning, and Help, with the existing exact-image installer guards. See [the new apps' controls and evidence](HID_AND_LEARNING.md). `emulation/verify_headless.py` checks all four linked ELF images and confirms Klatt is present while M5Unified/M5GFX/LovyanGFX/ST7789, FFT and eSpeak are absent. The learner additionally has no linked HID transport. Exact final lengths/hashes are in `dist-headless/manifest.json`. Earlier emulator observations above remain observations of their recorded workload images.

| Actual headless image | Binary bytes | Linker-reported static RAM |
| --- | ---: | ---: |
| Player | 1,600,160 | 91,788 bytes |
| Catalogue launcher | 696,032 | 67,016 bytes |
| Bluetooth/USB keyboard and mouse | 992,608 | 104,760 bytes |
| Key learning | 630,544 | 66,800 bytes |

A startup probe of the actual board firmware was also attempted with generic I²C register devices substituted for the keyboard/codec. The initial player probe exposed an unused visualizer FFT global constructor producing a coprocessor exception in this emulator. That constructor and its state were removed from the headless variant, saving another 4,632 static bytes. The final player and launcher probes reached application loading but did not report board initialization or complete their UI startup within 10 emulator seconds. Those attempts are **incomplete**, despite the emulator's normal timeout exit code; they do not establish that the full board firmware works. Their logs are retained separately from the successful substituted-output workload.

Static RAM can increase while runtime headroom improves: the fixed Klatt arena and audio staging are counted statically, whereas much of eSpeak's work memory was allocated later. Static-RAM percentage alone does not measure the improvement. The core benchmark is not a full runtime RAM measurement of the actual headless player with physical peripherals.

## Reproduction and tests

```powershell
python run_host_tests.py
python emulation/render_klatt_sample.py
python emulation/test_workload.py emulator-klatt
python build_package.py --headless
python emulation/verify_headless.py
```

Seven host suites passed integration safeguards, semantic observer, real player adapter with fake state, installer transfer, actual headless board glue with fake hardware, and both original speech output-channel configurations. Board tests cover key remapping/modifiers, unchanged input, codec/pin configuration, bounded packet copying, invalid packets, queue-full handling, stop/reset and consumed button events. They do not establish electrical behavior or physical mixer output.

The real Klatt renderer additionally passed PCM generation, completion, cancellation and failed-output checks. Longer phrases initially exceeded the fixed arena as single units; the successful host sample and firmware share the bounded-fragment renderer.

`headless-klatt-test-package.zip` is separate from the retained graphical/eSpeak ZIP. Its launcher accepts its own paired player hash. The SD file remains `accessible/media-player.bin`; native execution requires installation into internal flash. Notices and a voice preview are included. The initial-install image is for a dedicated test installation with backup/recovery, not an automatic data-preserving upgrade.

## Assessment before purchase

The changes substantially improve the software evidence: a bounded smaller backend completes the tested no-PSRAM music/Wi-Fi workload, and real player/launcher binaries exclude graphics code. The earlier eSpeak failure is not an unavoidable limit of every possible design.

This remains experimental. Physical keyboard/audio glue, codec, SD installation, boot/return, audible latency, active Wi-Fi traffic, long-term heap stability and battery life are unresolved. The emulator deliberately substitutes hardware and does not prove these properties. This is a more promising route to continue evaluating before treating a purchase as a dependable accessible music-player solution.
