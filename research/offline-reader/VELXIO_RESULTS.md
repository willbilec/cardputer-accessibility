# Velxio Cardputer ADV test, 2026-11-04

Follow-up: the isolated [BackgroundAudio/eSpeak-NG diagnostic](BACKGROUNDAUDIO_RESULTS.md) subsequently produced clear live speech in the user's listening tests, including difficult text, Wi-Fi scanning, rapid interruption and stopping. The negative Klatt results below remain valid for those earlier images; they do not describe the new engine.

**Velxio is more useful for this project's audio testing than the previous emulator, but our live Klatt speech is not usable in the tested configuration.** It runs the actual I²S driver without the previous handshake workaround, and the user heard clear prerecorded Klatt speech through our real board-audio code. The actual key-learning firmware was audible but extremely crackly, stuttering and slow. A diagnostic buffering change did not solve it.

No Cardputer was connected. The listening reports below are from the user hearing their computer's simulator output, not from a physical Cardputer speaker or headphone jack. They are stronger evidence than silent fake-output tests, but remain simulator evidence.

## What ran

The [hosted Cardputer ADV speaker example](https://velxio.dev/example/cardputer-adv-m5-buzzer) was opened in the in-app browser without creating an account or saving a public project. Its existing sketch compiled from the service's build cache, booted and displayed “Buzzer Test.” The user confirmed it was playing audio. The [vendor's board documentation](https://velxio.dev/docs/boards/reference/cardputer-adv/) describes ES8311/I²S audio and keyboard emulation; the trials below tested those claims for our inputs.

Runtime console messages identified **in-browser JS for cardputer-adv**. This trial did not use a locally installed native QEMU/libqemu backend. Browser diagnostic rates during live synthesis were around 14–19 million steps/second, with reported simulated-time progress around 88–96% of real time. Those counters are not a calibration against physical ESP32-S3 execution speed.

The simulator's File → Upload firmware control accepted our locally built merged binary files. The source editor remained the stock sketch, but Run used the explicitly uploaded binaries, evidenced by unique serial checkpoints from each image. The SDK-only I²S probe returned success from channel creation, standard-mode setup **and channel enable**, then printed `I2S_CONTROL_COMPLETE`. It needed no debugger register intervention. This independently strengthens the previous [emulator-handshake diagnosis](AUDIO_RESEARCH.md).

The actual key-learning app binary was then wrapped in an emulator-only factory-slot image. Its application bytes matched the current SD package. It reported:

```text
HEADLESS_BOARD keyboard=1 audio=1 heap=267472
KLATT_READY heap=258372 largest=208884 arena=32768
FIRST_PCM latency_ms=273
```

The user reported audible speech that was “insanely stuttery and crackly and slow,” still reading about twenty seconds later. This is a **failed usability result**, despite successful initialization and PCM generation. Clicking a learning key caused additional synthesis checkpoints, but key-label intelligibility and practical navigation were not accepted.

## Controlled comparisons

A separate diagnostic uses the same real lightweight board driver, mixer and I²S output. It plays the previously generated Klatt waveform from flash, removing live synthesis cost. The fixture is 186,780 mono 16-bit samples at 22,050 Hz: **8.4707 seconds**. Its WAV SHA-256 is `80a6a4f628559a092633b470fc5c85c12cc9d7847b54acc94ad8c7cc6a51fb24`.

| Test | Serial result | User listening report |
| --- | --- | --- |
| Prerecorded waveform, 2,048-sample chunks, drain after each | 92 blocks, 8,437 emulated ms, `ok=1` | Much clearer and smoother |
| Same waveform, 256-sample chunks, drain after each | 730 blocks, 8,438 emulated ms, `ok=1` | Still clear and smooth |
| Live “Keyboard ready.”, original drain-per-chunk service | First PCM 105 emulated ms; service finished in 7,720 emulated ms | No separate rating collected for this short baseline; full learner already rated unusable |
| Live “Keyboard ready.”, generation-ahead diagnostic | First PCM 106 emulated ms; service finished in 9,261 emulated ms | Still slow and badly stuttering |

Both prerecorded runs ended with the same reported free heap of 258,532 bytes. The firmware's transfer-completion time can precede the final hardware DMA tail; these numbers are not recordings of acoustic duration. The live phrase differs from the prerecorded fixture, so their durations must not be compared as identical speech material. All timings are single-trial simulator observations, not physical latency or statistically controlled performance measurements.

The queued variant uses the existing bounded queue, drains at utterance end and stops stale queued speech between generations. It was enabled only by `READER_PIPELINED_AUDIO` in a diagnostic environment. It did **not** improve the listening result and is **not enabled in the SD apps**. The SD firmware images remain the previous DMA-auto-clear build; only its packaged reports are refreshed to include these limitations. This experiment is not presented as a new production fix.

## What the comparisons establish

The Klatt-generated waveform, mono sample format, simple rate conversion and I²S output can produce clear simulator speech. Small 256-sample chunks alone did not reproduce the defect when samples were already available. The failure appears when synthesis runs live, implicating generation cost, scheduling or their interaction with streaming output. Merely removing each chunk's drain wait was insufficient.

These tests do **not** distinguish intrinsic target CPU cost from the browser emulator's execution/timing behavior. They also do not establish a physical driver failure. The next useful diagnostic is to measure generation time separately from output wait time for identical text, then compare a native CPU workload and the browser runtime. A cheaper synthesizer, optimized synthesis or bounded look-ahead may be needed; none is assumed solved here.

## Inputs and reproduction

- `emulation/prepare_velxio.py` wraps the packaged key learner and launcher; it records exact app/wrapper hashes in `test-results/velxio/inputs.json`. The launcher wrapper was prepared but **not run in this session**.
- `emulation/prepare_audio_compare.py` builds the waveform comparison; `--pipeline` builds its alternative output scheduling. The tested baseline wrapper is preserved as `test-results/velxio/audio-compare-simulator.bin`; later script naming includes the environment name.
- Input metadata and build logs remain under `test-results/velxio`. `observations.json` records the inspected UI checkpoints and user listening reports.
- `emulation/verify_velxio_records.py` checks saved input hashes, identity with the packaged learner and consistency of the transcribed observations. It does not automate a listening judgment. All fourteen existing host suites also pass; those passes do not override the failed audio-usability result.
- Diagnostic commands are `a` for prerecorded 2,048/drain, `b` for prerecorded 256/drain, `d` for prerecorded 256/queued and `c` for live “Keyboard ready.” Command `d` is available but was **not tested** here.

The firmware wrappers put an app in the existing factory slot for convenient simulator import. They are **not physical-device installation images**, and do not establish firmware switching or recovery. Diagnostic uploads did not flash hardware. No Bluetooth, active Wi-Fi traffic, long-term stability or SD installation trial was performed in Velxio.

## Assessment

Velxio closes a meaningful test gap: actual initialization and audible samples can now be exercised instead of bypassing I²S. It has also exposed a material live-speech performance problem that the earlier substituted-output tests did not establish. The current speech implementation should not be described as ready or dependable for accessibility before purchase. Physical hardware remains untested.
