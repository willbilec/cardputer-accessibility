# BackgroundAudio speech trial

Follow-up: [independent music, connected Wi-Fi and mixer fixes](COEXISTENCE_RESULTS.md), plus an [actual community-app adaptation with keyboard interaction](CARDEX_ACCESSIBILITY.md). The initial results below remain specific to those earlier minimal images.

2026-11-04. **Live speech was clear in Velxio for short phrases, menus, a difficult paragraph, Wi-Fi scanning, rapid replacement and explicit stopping. No physical Cardputer was tested.** This is a substantial improvement over the earlier [Klatt trial](VELXIO_RESULTS.md), which the user found extremely slow and crackly.

## Basis and implementation

[OpenVocakey](https://github.com/Milk-Cool/OpenVocakey) supplies a relevant Cardputer ADV implementation using [BackgroundAudio](https://github.com/earlephilhower/BackgroundAudio), an optimized eSpeak-NG implementation with incremental synthesis and its own I²S output task. Its [build configuration](https://github.com/Milk-Cool/OpenVocakey/blob/00ef8f89f5f147dc07e167dd7f77969e4d5bb725/platformio.ini) and [patch script](https://github.com/Milk-Cool/OpenVocakey/blob/00ef8f89f5f147dc07e167dd7f77969e4d5bb725/patchlib.py) provide three concrete changes: `-O3`, requested output buffers reduced from five to one, and the phoneme list reduced from 1,000 to 250. This source demonstrates an implementation route; it is not a published English latency benchmark.

Our isolated diagnostic pins BackgroundAudio 1.4.4 at `94641100866beae2cc97dc925c0d4562ad168879`. `prepare_background.py` checks the clean upstream revision and hashes before adapting a generated copy. The upstream checkout remains unchanged. PlatformIO's environment dump confirms `CCFLAGS` contains `-O3`, with `-Os` removed. The library internally splits the requested output into at least two DMA buffers, as required by ESP-IDF.

The app uses English at 175 words per minute, the ADV audio pins 41/43/42 and codec initialization on I²C pins 8/9. It loads no graphics, M5 board framework, Klatt engine, MP3 decoder or Bluetooth. The first diagnostic also excluded Wi-Fi. Later tests explicitly add Wi-Fi station scanning. This changes the synthesis engine, driver and configuration together; the trial does not isolate which individual change produced the improvement.

## Initial minimal trial

The simulator ran the compiled firmware, not browser-generated speech or a prerecorded clip. The user heard these announcements through the computer's simulator audio:

| Phrase | First non-silent PCM, simulated ms | Library done, simulated ms | Reported underruns | User listening |
|---|---:|---:|---:|---|
| Keyboard ready. | 34 | 756 | 0 | Clear and responsive |
| Accessible programs. Media player. Keyboard and mouse. Keyboard learning. | 47 | 4,779 | 3 | Clear and responsive |

These are firmware `millis()` readings. First PCM means submission to the output driver, not audible onset. `done()` is the library's completion indication, not a measured end of audible playback. The DMA frame counter is cumulative. Neither simulated timing nor the browser CPU's speed is a physical-device latency measurement. Three reported underruns in the longer trial remain a concern despite the positive listening report.

Initial firmware: 1,514,880 bytes; static RAM: 90,120 bytes. Free heap at ready: 250,708 bytes; minimum observed free heap: 236,260 bytes; largest reported free block: 204,788 bytes. These are simulator values for this diagnostic, without other app features. The first image SHA-256 and observations are preserved in `test-results/backgroundaudio/first-trial-observations.json`.

## Reproduction and expanded tests

Run `python emulation/build_background_trial.py` in this directory. The script builds `backgroundaudio-minimal` and creates `test-results/backgroundaudio/backgroundaudio-simulator.bin`. That merged image is **only for the simulator**, not an installation or migration image for a physical device. The current `inputs.json` identifies the latest build; it may differ from the initial image above.

In Velxio, use File → Upload firmware, select that image, then Run. Startup says “Keyboard ready.” Serial commands:

- `1`: short ready phrase; `2`: menu; `3`: key descriptions.
- `4`: longer paragraph with a filename, numbers, calculation, timer and warning.
- `5`: eight selection announcements at 300 simulated millisecond intervals, replacing earlier speech, ending with a distinct final selection.
- `w`: initialize Wi-Fi station mode and start an asynchronous scan; `x`: turn Wi-Fi off.
- `s`: stop speech and cancel the burst.

Wi-Fi scanning tests simulator driver allocation and scheduling; it does not demonstrate physical radio coexistence, a successful internet connection or sustained network traffic. No credentials are entered. This diagnostic is not yet integrated with the keyboard learner, media player or launcher, and the SD catalogue has not been changed.

The expanded image is 2,094,320 bytes, with 112,268 bytes of static RAM. Its SHA-256 is `c70732eb2a3ca990c61ad9cef8311a89272b02abcd968e4290ab7287c29cdef4`. It fits the diagnostic factory partition (2,621,440 bytes); PlatformIO's percentage uses the larger upload-size setting instead, so the build harness checks the real partition bound explicitly.

With Wi-Fi initially off, the expanded app reported 220,888 bytes free at ready and 206,436 minimum. The difficult paragraph produced first non-silent PCM after 29 simulated ms, library completion at 24,367 simulated ms, 14 reported underruns and 222,264 bytes free afterward. The user reported it was clear and reached the fox-and-dog sentence, which is the intended ending. The underflow count means this is not a claim of flawless buffering.

### Harder workload results

| Test | Simulator result | User listening |
|---|---|---|
| Same difficult paragraph with station mode and a concurrent scan | First PCM 52 simulated ms; done 24,389 simulated ms; 14 underruns | Still clear |
| Eight speech replacements at 300 simulated ms intervals, Wi-Fi still enabled | First PCM 12–15 simulated ms per request; final phrase completed; 2 underruns during final phrase | Prompt cuts and clear ending |
| Turn Wi-Fi off, start paragraph, then explicitly stop | `BG_STOP` appeared before paragraph completion | Stopped promptly in the middle of “battery” |

The scan reported five simulated networks. After scanning/speech and after the replacement burst, free heap was 168,716 bytes, minimum 154,140 and largest block 126,964. After turning Wi-Fi off, free heap was 203,796 before the cancellation trial; the largest block remained 126,964. This did **not** restore all 222,264 bytes from before Wi-Fi initialization. One cycle cannot establish a memory leak or leak-free operation. No panic/reset appeared during these trials. See `test-results/backgroundaudio/stress-observations.json` for the transcribed checkpoints and listening responses.

The scan overlaps the beginning of the long utterance; the remainder runs with Wi-Fi enabled. It is not sustained scan pressure or connected network traffic. Rapid replacement and Stop are directly useful screen-reader behaviors, but keyboard navigation and the complete reader applications remain separate integration work.

A short phrase successfully restarted after Stop: first PCM 60 simulated ms, library completion 783 simulated ms, zero reported underruns, free heap 203,796 bytes. The simulator was then stopped, with the diagnostic image retained for further work. Image SHA-256 and JSON record parsing were verified; guarded dependency generation was idempotent and the upstream checkout stayed clean.

## Other researched routes

- [NucleoOS](https://github.com/indecenti/NucleoOs/blob/main/docs/tts.md) uses offline speech clips generated on a PC, indexed and streamed from SD. This avoids live synthesis work, but its documented fallback to reading unknown text on screen would need replacing with speech or spelling for this project.
- [TypingFighter](https://github.com/Alex-Wei-HZ/DistributionResources/blob/main/TypingFighter/v0.1.0-rc.2/README.md) distributes an ADV speech asset pack for a fixed vocabulary. This supports the prerecorded-prompt approach, rather than proving arbitrary offline synthesis.
- [VoiceTerminal](https://voiceterminal.dev/docs) streams speech generated on a server. A locally running server can avoid cloud dependence, but it is not standalone speech generation on the ADV.

These are alternatives if the full applications cannot meet their resource budgets. The BackgroundAudio route is the strongest next live-synthesis candidate from this trial.

The remaining purchase decision depends on real ADV speaker/headphone audio, interruption responsiveness, memory under the complete apps, Wi-Fi traffic and extended operation. The positive simulator result makes further work worthwhile; it does not establish those hardware properties.
