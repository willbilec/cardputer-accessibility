# Building Cardputer ADV firmware with offline speech

This guide describes the BackgroundAudio/eSpeak integration for the
**Cardputer ADV**, which has an ESP32-S3, 8 MB flash and no PSRAM. Its
CardEX examples come from the emulator work. Later physical diagnostics
confirmed short speech, keyboard and mixed-audio trials on the ADV.
See [physical results](research/offline-reader/PHYSICAL_RESULTS.md).
For a built-in app, start with the [app authoring guide](ACCESSIBLE_APP_GUIDE.md).

Adding speech synthesis makes text audible. A screen-reader-style interface also needs semantic information: which item is selected, what a control does, whether a value changed, what input is expected and whether an operation succeeded. Calling speech from every screen redraw produces repetition and interruption. Announce changes in the application state instead.

## 1. Choose and pin the speech engine

The successful prototype uses [BackgroundAudio](https://github.com/earlephilhower/BackgroundAudio), an incremental eSpeak-NG implementation, rather than this project's earlier Klatt service. Pin **BackgroundAudio 1.4.4**, commit:

```text
94641100866beae2cc97dc925c0d4562ad168879
```

The relevant community precedent is [OpenVocakey](https://github.com/Milk-Cool/OpenVocakey). Its optimized build and memory reductions led to the successful minimal trial here. That source is an implementation precedent, not a physical-device performance benchmark for this project.

Run the guarded dependency preparation from the workspace root:

```powershell
python research/offline-reader/prepare_background.py
```

It checks the upstream revision, clean status and normalized file hashes, then adapts a generated copy at `research/offline-reader/generated/backgroundaudio-trial`. It leaves `research/BackgroundAudio` unchanged. The changes are:

1. Reduce the speech engine's requested output buffers from five to one. A particular output adapter can increase this again when needed.
2. Reduce `N_PHONEME_LIST` from 1,000 to 250. Long utterances and unusual text must still be tested; a smaller internal limit is not a promise of unlimited input.
3. Correct the mixer's `availableForWrite()` to return **bytes**, as its interface promises, rather than 32-bit stereo words.
4. Preserve the configured input sample rate when a mixer input starts. Otherwise, a 22,050-Hz speech source can be played at the mixer's 44,100-Hz rate, producing a high, fast voice.

The latter two changes affect the mixed-audio path. They were checked against the actual mixer header with sanitizer tests. Do not blindly apply these replacements to a different library revision. Inspect whether a newer release already fixes them. Retain the upstream license notices when carrying the dependency and adapted sources.

## 2. Use the tested toolchain profile

The experiments use Arduino ESP32 3.3.9 through this pinned pioarduino platform package:

```ini
platform = https://github.com/pioarduino/platform-espressif32/releases/download/55.03.39/platform-espressif32.zip
framework = arduino
board = esp32-s3-devkitc-1
board_build.arduino.memory_type = qio_qspi
board_build.flash_size = 8MB
board_upload.flash_size = 8MB
build_unflags = -Os
build_flags =
    -O3
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    symlink://path/to/generated/backgroundaudio-trial
```

Resolve the dependency path relative to the new project. Add the libraries that the application actually needs. Do not enable `BOARD_HAS_PSRAM` for the ADV. Use C++17 or later if copying the word-boundary chunk helper, which uses `std::string_view`.

This is a profile excerpt, not a complete installation layout. Select and verify a partition table separately. The project contains merged images with **simulator-only layouts**; do not flash one over an existing physical installation. An application-only `firmware.bin` and a merged factory image are different artifacts.

## 3. Give one component ownership of audio

The tested ADV output wiring is:

| Signal | GPIO |
|---|---:|
| I²S BCLK | 41 |
| I²S word-select / LRCLK | 43 |
| I²S data to codec | 42 |
| Internal I²C SDA | 8 |
| Internal I²C SCL | 9 |

The ES8311 codec is at I²C address `0x18`. When M5Stack's board framework already owns the internal bus, use **`M5.In_I2C`** for codec register access. Do not independently initialize another I²C controller on the same pins after the keyboard driver starts.

Disable automatic M5 speaker/microphone initialization when BackgroundAudio owns I²S:

```cpp
auto cfg = M5.config();
cfg.internal_spk = false;
cfg.internal_mic = false;
cfg.fallback_board = m5::board_t::board_M5CardputerADV;
M5Cardputer.begin(cfg, true);
```

Remove or redirect later calls that start `M5Cardputer.Speaker`. Two output drivers competing for the same codec/pins are not a mixer. The tested prototype's codec register sequence is in [Reader.cpp](research/CardEX/src/Reader.cpp). Treat it as a tested software recipe for this experiment, not a complete physical speaker/headphone acceptance result or a replacement for the vendor's codec documentation.

The M5 board-detection fallback and keyboard setup require separate verification. In our emulator profile, the normal interrupt-based TCA8418 reader did not consume queued events. [SimulatorKeyboard.h](research/CardEX/src/SimulatorKeyboard.h) polls the FIFO instead, with one event per update so that a press and release cannot disappear between application snapshots. This is an **emulator profile**, not a requirement proven for physical hardware.

## 4. Start with speech alone

Use [backgroundaudio_minimal.cpp](research/offline-reader/src/backgroundaudio_minimal.cpp) as the small executable example. It tests codec initialization, an English voice, short and longer utterances, interruption, Wi-Fi allocation and heap logging. Its standalone board setup uses Arduino `Wire`; an app using M5Stack's existing I²C bus should use the shared-bus approach above instead.

The essential library calls are:

```cpp
#include <BackgroundAudioSpeech.h>
#include <ESP32I2SAudio.h>
#include <libespeak-ng/voice/en.h>

ESP32I2SAudio output(41, 43, 42);
BackgroundAudioSpeech speech(output);

// Initialize the board and codec first.
speech.setVoice(voice_en);
bool started = speech.begin();
speech.setRate(175);
speech.setGain(0.6f);
speech.speak("Application ready.");
```

Check the underlying output driver's initialization result as well as `speech.begin()`: the library's high-level return alone did not provide all the diagnostic evidence we needed. Keep a clear failure path in serial diagnostics. A failed speech initialization cannot reliably announce its own failure through that same audio path.

Use **one speech instance**. The embedded eSpeak implementation has global synthesis state; this guide does not establish safe concurrent independent eSpeak instances. Call the application-facing speech service from one UI task, rather than arbitrary interrupt handlers or several competing application tasks.

## 5. Add a small semantic speech service

The working reference consists of:

- [Reader.h](research/CardEX/src/Reader.h): the UI-facing interface.
- [Reader.cpp](research/CardEX/src/Reader.cpp): audio ownership, voice, focus deduplication, announcements, continuation, interruption and statistics.
- [ReaderChunks.h](research/CardEX/src/ReaderChunks.h): bounded text chunks that prefer word boundaries.

The existing files are guarded by the historical `CARDEX_SPEECH` build flag. When reusing them, rename that guard for the new firmware or define it deliberately in the new profile. Keep hardware initialization appropriate to the new application; do not copy the entire CardEX program just to obtain speech.

The API is intentionally small:

```cpp
Reader::begin();
Reader::event("Settings saved.");                  // Important announcement
Reader::focus("menu/volume/5", "Volume, 5 of 10."); // Changed focus/state
Reader::context("Full current input and position"); // Repeatable context
Reader::repeat();
Reader::next();
Reader::stop();
Reader::poll();                                    // Call regularly
```

`focus` needs a stable identifier that changes when the meaningful selection or value changes. Do not use a frame counter: that would announce every redraw. Conversely, do not identify every menu item only by the menu name: different selections would then be suppressed.

For example, announce the selected item from navigation logic:

```cpp
void announceSelection() {
    const auto& item = items[selected];
    Reader::focus(
        "tools/" + String(selected),
        String(item.label) + ". " + String(selected + 1) +
        " of " + String(itemCount) + ". Enter opens.");
}
```

Call it after selection changes and when a screen opens. A text renderer cannot tell you whether a label is focused, disabled, an error, a password or an unrelated decorative caption.

## 6. Define what each interaction speaks

Before modifying screens, write a short speech contract for each state:

| Interaction | Speech |
|---|---|
| Enter a screen | Its purpose, selected item and essential controls |
| Move selection | Label, relevant state and position |
| Change a setting | Label and actual new value |
| Edit text | Character/word feedback, caret movement and repeatable full context |
| Calculate | Result, or the actual error after submission |
| Confirm an operation | Target, consequence and accept/cancel choices |
| Start a long operation | A brief starting status |
| Report progress | Coarse updates, not every loop iteration |
| Finish or fail | Explicit completion or actionable error |

For a calculator, speak operators and structural punctuation meaningfully. Reading `2+2` as “two two” is not sufficient. Distinguish minus signs, division, parentheses, powers and equality. Do not announce every temporary parse error while a person is still typing an unfinished expression.

Do not automatically speak password characters or secret values. Sensitive prompts need an explicit policy. Regular keyboard-emulator typing should also remain silent where the firmware cannot know whether a remote field is a password.

Provide a way to repeat context, stop speech and obtain help without leaving the current screen. Describe unavailable features explicitly. A visual graph needs data inspection or a useful textual representation; an image-viewer title is not a description of its pixels.

## 7. Handle long text and interruption deliberately

The service bounds each speech request to approximately 280 source characters and prefers word boundaries. Important messages and help continue automatically through all chunks. Continuations are queued without flushing the previous chunk's audio tail. Repeatable long context can be read in further chunks with the reader's “more” command.

This behavior matters: an early implementation simply truncated help at the request limit. The user heard an incomplete final word. That was an application bug, not a limitation we should hide in the documentation.

During an important announcement, the service retains the **latest** pending focus instead of accumulating every intermediate selection. Explicit Stop or repeat can interrupt. Closing a dialog stops its obsolete announcement. Keep `poll()` running inside modal input loops as well as the normal main loop, or continuation and deferred announcements will stop progressing.

The current direct-output profile requests **two output buffer frames**. At 1,324 frames per buffer and 22,050 samples/second, that represents roughly 120 ms of capacity before driver-specific splitting. This is a capacity calculation, not a measured end-to-end Stop delay. Larger buffers can cover scheduling gaps but retain more queued audio. Three frames improved subjective smoothness; a subsequent Stop test was confounded by agent/browser-control timing. The final two-frame build was independently operated by the user and described as working pretty well.

Check both smoothness and interruption. An underrun counter is useful, but zero driver underruns does not prove the browser's audio presentation never paused. Startup, SD work and heavy computation may behave differently from an idle menu.

## 8. Mix other audio through the same output

[backgroundaudio_coexist.cpp](research/offline-reader/src/backgroundaudio_coexist.cpp) is the executable mixed-audio reference. It uses the library's mixer, a live speech input and an independent WAV decoder. Do not start a second hardware output driver.

The mixer speech input needs at least two software buffers in this prototype: one buffer can deadlock when startup silence fills only part of the frame. Apply the mixer byte-count and sample-rate fixes described earlier. The tested output mixer runs at 44,100 Hz, while speech runs at 22,050 Hz. Preserving that source rate is essential to keeping normal voice pitch.

The user heard clear music and speech together while the simulator reported Wi-Fi association and DHCP. That experiment used a repeating WAV in flash, **not** MP3 decoding from SD. Outbound HTTP attempts failed, so it does not establish sustained internet-traffic coexistence. See [the exact results](research/offline-reader/COEXISTENCE_RESULTS.md).

## 9. Measure and test each layer

Record the exact image hash, dependency revisions, build flags and workload. Measure free heap, minimum free heap, largest free block, task stack headroom, output underruns, first non-silent PCM submission and completion. Do not report a static linker RAM figure as peak runtime RAM.

Exercise lazy allocations and failure paths, not just successful examples. In the [Cardulator adaptation](research/cardulator/ACCESSIBILITY.md), a mile/km conversion succeeded through a direct handler, but a different conversion loaded the full GNU Units catalogue and fatally exhausted RAM. Its speech profile now uses an explicit compact catalogue. Check database loads, script compilation, searches and error paths while speech is initialized. A library may terminate on allocation failure rather than return an error.

Finish blocking startup work before starting an automatic announcement, and call the speech service regularly afterward. Cardulator initially spoke during SD/settings loading; moving the announcement afterward was sensible but did not eliminate the user's reported 10–20 seconds of initial crackle. Driver counters reported no underruns despite that listening result. Record audible issues even when counters look healthy; do not assume that an emulator glitch or hardware behavior has been identified without evidence.

Use these separate kinds of evidence:

1. **Build:** size, symbols, partitions and successful linkage.
2. **Host tests:** actual selection/input/queue/chunk logic, including bounds and sanitizer checks.
3. **ESP32 emulator:** firmware execution and measured simulated state; note peripheral limitations.
4. **Audible simulator:** actual listening, keyboard navigation and real screen-reader interaction with the browser.
5. **Physical ADV:** actual speaker/headphones, responsiveness, memory, Wi-Fi, SD, battery and installation/recovery.

Only the last category establishes physical-device behavior. Simulated firmware `millis()` is not an audible wall-clock latency measurement. A remote agent pressing a key and asking when it stopped also includes control and presentation delays.

For reproduction, the workspace has:

```powershell
cd research/offline-reader
python emulation/build_background_trial.py
python emulation/build_background_trial.py --coexist
python emulation/build_cardex_trial.py
python emulation/build_cardulator_trial.py
python run_host_tests.py
```

Run PlatformIO builds sequentially within a project. Preserve previous logs and image identities when comparing variants. Check the generated partition table against the merged image before treating its layout as correct.

## 10. Hand the emulator to a screen-reader user

In Velxio, choose **File → Upload firmware**, select the merged simulator image, then **Run**. Uploading alone does not start it. The code editor can still display the original example while the uploaded binary runs; confirm the unique serial startup messages.

The emulated board needs keyboard focus. Its graphical key labels are not ordinary accessible buttons. In our Edge test, the user initially had difficulty getting keys through, then successfully operated CardEX after resolving focus. With NVDA, its documented **NVDA+F2** command passes the next key to the active application. This is a troubleshooting option, not proof that the entire simulator website is accessible.

Use the person's normal browser when the embedded browser is difficult to navigate. Keep only one audio-producing emulator running. Confirm that the person can independently select, open, edit, stop and recover—not only that automation can send keys.

## What this does not provide

This is a reusable offline speech and semantic-announcement approach for firmware you can modify. A launcher cannot keep its speech task running after switching to a separate firmware image. Each image needs its own accessibility integration. The service is not an accessibility tree, OCR engine or MicroPython binding, and it cannot universally understand arbitrary compiled firmware.

The physical diagnostic results cover specific short workloads. Firmware
switching and recovery under the experimental layouts, headphone acceptance,
complete multilingual support and long-running workloads remain open.
