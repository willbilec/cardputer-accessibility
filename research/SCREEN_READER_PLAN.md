# Reusable Cardputer screen reader: implementation plan

Research date: 2026-11-03. This is an architecture and implementation plan, not an implemented screen reader or a device acceptance result.

Follow-on implementation: [headless Klatt results](offline-reader/HEADLESS_RESULTS.md) describe a smaller speech backend, graphics-free player/launcher builds, successful core emulator workloads and remaining hardware limitations. The plan below records the original architecture investigation.

## Answer to the main question

We can reuse most of the screen-reader implementation across firmware, without placing speech calls on every screen or every line of code. The practical design is a common screen-reader library, a graphics-library text capture adapter, and a small application adapter that supplies selection, state, full labels and sensitive-field information.

It will not automatically make arbitrary, already-compiled firmware accessible just because Launcher starts it. Launcher boots a separate application image. Its tasks, text hooks and speech engine do not remain running alongside that application. ESP-IDF's normal boot selection chooses an application and restarts into it; the second CPU core is part of that running firmware's shared system, not an independent place to preserve Launcher across reboots. This conclusion follows from the normal application boot/task model, not from a device experiment. See [ESP-IDF OTA documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/ota.html) and [ESP-IDF multicore documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/system/freertos_idf.html).

Therefore:

- **Separate native firmware:** build the shared reader into each supported firmware. It runs in the background while that firmware runs.
- **Apps in a common runtime:** integrate the reader once into the host, such as an accessible MicroHydra fork, then give its Python apps a common accessibility API and display adapter. Apps still need selection/state support when the toolkit cannot supply it.
- **Existing unmodified binaries:** do not promise speech. A launcher cannot recover a reliable accessibility model from their pixels or install a universal runtime hook into arbitrary binaries.

A resident supervisor or custom operating system that hosts rewritten applications is a different, much larger project. A bootloader patch, second-core task, shared flash address or OCR does not provide that capability by itself. An external reader could offload synthesis only if the firmware sends it text/events; it still needs an integration point.

## What the example media player actually exposes

Examined a clean, shallow source checkout in `research/MP3PlayerForM5Cardputer`, upstream commit `b7548630a222403424386f2d298043e360661d6a`. Upstream files were not modified. References below are pinned to that revision's [main sketch](https://github.com/sanchitminda/MP3PlayerForM5Cardputer/blob/b7548630a222403424386f2d298043e360661d6a/MP3PlayerM5Cardputer.ino) and [README](https://github.com/sanchitminda/MP3PlayerForM5Cardputer/blob/b7548630a222403424386f2d298043e360661d6a/README.md).

| Area | Existing source information | Accessibility consequence |
| --- | --- | --- |
| UI routing | `UIState` at line 75: player, settings, help, Wi-Fi scan, text input, folder selection and search | Observe the state machine instead of speaking every rendering call. Also inspect `BootMenu` and startup paths outside these seven states. |
| Playlist | `AudioEngine` lines 437–453 has separate `browserIndex`, `currentIndex`, pause/shuffle/loop and metadata. `drawPlaylist` lines 870–927 draws selection by color and truncates filenames | Speak the selected full filename and position from the model; distinguish browsing from the playing track. Captured text alone loses the full name. |
| Search | `drawSearch` lines 713–785 uses `g_searchCursor`, result indices, and shortened filenames | Expose query, result count and selected full result. Do not read all visible results after every key. |
| Settings | `drawSettings` starts at line 1284; `settingsCursor` selects labels and values | Extract a shared item-description helper used by both drawing and accessibility, avoiding a second settings-label switch that drifts. |
| Folders | `drawFolderSelect` lines 1345–1406 has a cursor, full folder name and cache state before shortening | Expose folder name, position, current status and meaningful cache status. Symbols alone are insufficient. |
| Wi-Fi | `drawWifiScanner` lines 1424–1435 has a cursor, SSIDs and empty-list message | Expose selected full SSID, scan progress/result and errors. Bound SSID text as untrusted input. |
| Text entry | `drawTextInput` lines 1437–1450 and input handling lines 2630–2644 distinguish client password, AP name and AP password | Both password targets must be sensitive. The source masks target 0 but draws target 2, the AP password, in plain text. A naive display reader could disclose it. Exclude secrets before capture, logging, history and speech; masking the visible AP password should accompany the eventual integration. |
| Help | `helpLines`, localized help loading and `drawHelp` | Provide next/previous line and read-all using the active language's content. Keyboard hints should match actual handlers; the README and sketch differ in some shortcuts. |
| Redraws | Main loop lines 2443–2458 redraws visualizer about every 30 ms and now-playing about every second | Drawing frequency must not become speaking frequency. Announce semantic changes; elapsed time is normally requested explicitly. |
| Screen sleep | Display sleeps at lines 2461–2462; pocket controls operate separately | Model-based speech should work with the LCD asleep. Screen capture can be stale when drawing stops. |
| Audio | `AudioOutputM5Speaker` lines 203–221 submits MP3 PCM through `Speaker.playRaw`; setup selects virtual channel 0 at line 2424 | The current speech prototype also uses channel 0. Directly combining them risks cancellation and interference. Establish shared audio ownership first. |
| Startup | Setup lines 2403–2416 configures the speaker, erases NVS on an initialization error and loops forever on SD initialization failure | ADV audio compatibility needs verification. Adapt failure handling to speak and offer recovery; do not exercise destructive NVS fallback on existing device data. |
| CPU load | `applyCpuFrequency` lines 336–339 selects 80/160/240 MHz; visualizers use sprites and FFT | Measure speech with playback, Wi-Fi and reduced CPU clocks. Successful synthesis in the standalone calculator does not establish playback coexistence. |

This is a good first native app because its state is already centralized. Its documentation targets Cardputer generally; this investigation has not established that this revision works unchanged on ADV's ES8311 audio hardware. No player build or device run was performed in this planning pass.

## Proposed architecture

Data flow: application model or toolkit events → bounded accessibility model → change detection and announcement policy → existing speech service through a pluggable audio sink. A graphics text mirror feeds screen review as a second source.

### 1. A small common accessibility model

Use a C++ library shared by Launcher and supported native apps. Do not require a large DOM or a complete copy of the song library. Describe the active screen and visible/reviewable items with stable IDs, role, full label, value, focused/selected/checked/disabled state, position/count, sensitivity and optional actions. Use bounded strings and explicitly report truncation or capacity exhaustion.

Distinguish keyboard focus, selected list item, playing item and the reader's review cursor. Review navigation must not move the application's selection unless the user invokes an action. For long lists, request items lazily through the app adapter; keep full selected labels without retaining thousands of songs in RAM. Define a bounded utterance limit and an explicit continuation mechanism for longer labels or documents.

Toolkit adapters can supply this model for all applications using that toolkit. Custom-drawn apps generally need one adapter for their existing state machine. This is the reusable abstraction that removes scattered `speak()` calls. Direct events remain appropriate for transient errors and progress that disappear before a snapshot.

### 2. Text capture for screen review

Prototype an optional M5GFX/LovyanGFX adapter at text decoding/layout boundaries, before glyphs become pixels. The local M5GFX checkout is `7cb26b44ab31bb41a0a597196dd2c6d868869a21`; `LGFXBase.hpp` contains `drawString` overloads around lines 724–726 and `write` around lines 916–917, with character rendering and UTF-8 writing in `LGFXBase.cpp`. These are investigation points, not verified interception coverage. Intercepting only `drawString` would miss the player's many `print`/`printf` calls.

The adapter should collect complete decoded text runs, surface ID, position/bounds and style into a bounded screen mirror. It must:

- Reassemble fragmented `print`/`printf` and UTF-8 input without speaking each character of a repaint.
- Track clear, overwritten regions, scrolling, clipping and screen replacement so removed text does not survive in review.
- Track sprite identity and display placement; text in an offscreen sprite is not visible until the relevant portion is composited. Handle repeated copies, invalidation and coordinate transforms conservatively.
- Avoid double capture when a higher-level string call reaches lower-level character drawing.
- Exclude decorative surfaces and transient visualizer content. Do not allocate or synthesize in pixel, glyph or audio callbacks.
- Redact sensitive input before it enters the mirror, including text drawn in sprites. For an unknown input field, do not automatically echo captured typed content without a trusted sensitivity declaration.
- Expose line/item review and a read-screen command. A simple top-to-bottom/left-to-right ordering is only a fallback; the player has independent panes and overlays, so semantic regions must override it.

Use explicit frame/screen boundaries when available. A timing debounce is a fallback, since many immediate-mode apps have partial updates and no frame commit. Deduplicate within a screen generation and preserve intentional repeat commands.

Color/highlight rules can assist a known app profile but must not define universal focus: this player changes themes, uses colors for both selection and playback, and sometimes uses text color without a filled selection rectangle. Pixels, images, custom font blits and arbitrary drawing are not recoverable text through this adapter. Other display libraries need their own adapters and a firmware rebuild.

Text capture is useful even without a complete app adapter, but the supported capability should be labeled **text review**, not full screen-reader access.

### 3. Announcement and keyboard policy

The reader compares stable IDs and semantic values, rather than drawing calls. Announce screen entry, current selection, value changes, committed input, important status, errors and coarse progress. Read headings and controls on demand. Suppress battery/clock/progress repaint chatter and coalesce repeated volume or seek changes.

Introduce explicit categories: focus/value, requested review, status/progress, and urgent error. Keep a small bounded scheduler: newer focus replaces stale focus; errors take precedence; repeated progress coalesces. Stop and repeat must have predictable behavior. Return-to-launcher and reboot announcements need bounded completion waits and a recovery path if audio fails.

The current `speech_service.h` is a UI-task API with replacement semantics, not a priority scheduler or accessibility model. Put the scheduler above it and make its cancellation generation explicit; route events from other tasks into the UI dispatcher. Never call it directly from ISR/audio callbacks. Prevent an unchanged model snapshot from interrupting an explicit repeat or read-all.

Provide reader commands for stop, repeat, current focus, next/previous review item, read screen, playback status, help and settings. Choose actual keyboard chords after auditing the app's handlers and testing ADV key combinations; do not reserve keys already used by the player. Reader commands are consumed once before application handling. Reader operation must continue with the LCD off. Expose keyboard echo preferences; passwords use the existing generic-character policy and cannot enter repeat history.

### 4. Shared audio output

Refactor speech synthesis from its hardware sink: the existing prototype's `SpeakerOutput` calls `playRaw` at 22,050 Hz on virtual channel 0. The player uses channel 0 for music and allows changing output rate. Neither may independently reinitialize the codec/I²S driver or issue a global stop that kills the other stream.

First verify the installed M5Unified version's mixer/channel, resampling, buffer ownership and cancellation behavior. Evaluate a reserved speech channel alongside music, with per-stream gain and music ducking. A channel number alone does not prove safe mixing or adequate CPU/heap. If that route fails, use one bounded PCM mixer and one output owner; a temporary pause/resume policy is a prototype fallback whose behavior must be stated and tested. Speech failure should leave transport controls usable.

Keep synthesis off the main UI loop, ensure MP3 decoding continues to receive time, and bound producer backpressure. Restore music gain after completion, cancellation, failure and rapid reader commands. Preserve independent speech and music volume. Configure ADV audio through a common board adapter; verify speaker/headphone behavior rather than copying original Cardputer setup assumptions.

### 5. Launcher integration and distribution

Launcher remains an accessible firmware selector and installer. Add versioned metadata for each tested artifact: board, image size/hash, reader ABI, supported capability (semantic access/text review/none), keyboard commands, audio policy, data requirements and return/recovery compatibility. Unknown firmware is announced as having unknown accessibility; a badge is not inferred from its display library.

Build scripts can apply pinned, reviewed adapters and rebuild supported open-source projects. This automates distribution of accessible variants, not accessibility of every catalog binary. Check licenses and preserve notices when distributing modified player builds; the checkout includes GPLv3 license text.

Use a versioned, dedicated settings namespace or an explicitly agreed SD file for reader preferences. Never repurpose another application's NVS or infer compatibility from a matching partition label. Source reuse still duplicates reader code/voice data across independently linked images. Shared flash voice resources could be researched later with a versioned read-only format, bounds checks and migration/recovery design; do not put function pointers into another firmware's flash or assume its code is safely callable.

The existing experimental 3 MiB launcher partition and roughly 2.23 MB diagnostic app images already make 8 MB flash tight. Recalculate with the actual player build, required data and aligned partitions. SD installation may allow choosing which firmware occupies flash, but does not imply arbitrary native execution directly from an SD file. No partition or bootloader changes are part of this plan's first prototype.

## First player adapter: limited integration points

Create one `PlayerAccessibilityAdapter` that reads an immutable snapshot after input and background state updates. Observe at least `currentState`, browser/playing indices, settings/folder/search/Wi-Fi cursors, metadata and transport state. It should work independently of redraw and LCD sleep.

Use shared descriptor helpers for settings and list labels. Cache the selected song's full path/name by playlist identity and index so unchanged snapshots do not add SD reads on every loop. Only fetch changed selections or explicit review requests. Detect metadata arriving later without repeating the entire screen.

Add explicit events at blocking/transient operations: SD failure, library scan start/progress/completion, no results, Wi-Fi scan/connection failures, invalid password length, saving and rebooting. Long scans and startup code need opportunities to service reader commands and speech; a once-per-loop observer cannot see events while the loop is blocked. Handle startup/BootMenu before the normal state observer begins. Do not announce secrets while scanning display calls.

An example desired interaction is: “Music. Example Song.mp3, 3 of 27”; Down → the next full filename; Enter → “Playing …”; settings navigation → “Wi-Fi power, off”; adjustment → “On”; reader status command → playing title, artist and elapsed time. These are proposed utterances, not test output.

## Implementation stages and completion evidence

1. **Portable semantic core and host harness.** Implement bounded nodes/events, snapshot diff, priority scheduling, sensitive redaction, stop/repeat and review commands with a fake speech sink. Test stable redraws, selection changes, repeated values, rapid navigation, overflow, screen replacement, explicit repeat, late metadata and sensitive histories. Use real adapter/reader code in these tests.
2. **Player semantic integration.** Establish a pinned, reproducible ADV build and baseline image/RAM usage. Verify the audio backend and initialization/failure paths, then add the single state adapter, shared descriptors and transient events. Cover the seven UI states plus startup/BootMenu, display sleep, pocket buttons, empty SD/library, long filenames, search, settings and password errors. Compile does not prove these paths usable.
3. **Optional graphics mirror experiment.** Capture actual M5GFX text/layout paths. Test `print`, `printf`, strings/chars, UTF-8 fragments, multiple panes, sprite copies, clears, scrolls, clipping, partial redraw, overlay dismissal and protected fields. Use recordings from the actual player renderer; a fake display alone cannot prove hook coverage. Compare usefulness with and without semantic hints. Keep auto-speaking off until privacy and redraw suppression are established.
4. **Reuse demonstration.** Adapt the calculator/timer to the same semantic API and integrate Launcher behind its ADV speech feature flag, retaining ordinary Cardputer builds. This establishes whether the architecture reduces repeated app work. Keep MicroHydra integration as a separate native-module/runtime milestone; do not present a C++ adaptation as the original Python app becoming accessible.
5. **On-device evaluation before installation expansion.** Verify connected hardware/port, bootloader, backups and recovery procedure. Establish that the player runs on ADV and audio works before combined testing. Use a dedicated test installation; preserve existing firmware/data. Test the matrix below and conduct keyboard/speech acceptance with the user. Only then assess firmware switching and safe packaging.

For launcher source changes, follow its own AGENTS.md/CLAUDE.md and build both `m5stack-cardputer-adv-access` and ordinary `m5stack-cardputer`. If partition tables change later, force merged-image regeneration and compare embedded/generated tables. Run the existing speech fake-audio tests when changing the service. Never run concurrent PlatformIO processes in `speech-prototype`.

## Target-device measurement matrix

Cross Wi-Fi off / associated idle / active local transfer with music stopped / playing representative MP3s, visualizer off/on, and supported 80/160/240 MHz modes. Include AP hosting/streaming as a distinct player workload. Exercise speaker and headphones, LCD on/off, rapid navigation, interrupt/repeat, long labels and an extended playback session. Record bitrate, sample rate, CPU mode, firmware hash, board/library versions and actual network workload; report unsupported combinations instead of silently substituting a different one.

Measure key-event to enqueue, synthesis start, first PCM submission and **audible** onset separately; PCM submission is not acoustic latency. Record p50/p95/max, sample count, UI processing delay, dropped/coalesced events, free/minimum/largest heap, task stack headroom, underruns, stream interruptions and resets. A recording or loopback with a synchronized input marker is needed for audible onset. Do not derive battery life from CPU settings; measure it separately.

Provisional usability targets for user review: median audible focus onset at or below 200 ms and p95 at or below 400 ms, stop response at or below 100 ms, no stale selection spoken after navigation settles, no secret in speech/log/history/mirror, and no reproducible music interruption caused by reading during a 30-minute combined-load trial. These are proposed acceptance targets, not achieved measurements or hardware guarantees. If eSpeak cannot meet memory/latency targets with decoding and Wi-Fi, compare a smaller engine or different audio policy using the same reader API.

## Evidence and limitations of this planning pass

Completed: inspected current project status, speech API/output, existing Launcher working-tree changes, the pinned player source and display/state/audio/input paths, and local M5GFX hook candidates; consulted Espressif's boot and multicore documentation. Downloaded player source as a retained research dependency.

Not completed: reader implementation, graphics interception, player compilation, audio mixing, runtime profiling, physical audio/keyboard/Wi-Fi tests, accessible firmware switching or migration/recovery validation. Earlier host/build results in PROJECT_STATUS.md remain evidence only for their stated diagnostic firmware and simulations.

Recommendation: start with the semantic reader core and this player's state adapter, while treating M5GFX text capture as an optional review facility. Aim for one reusable reader and a small adapter per UI framework/application family, with explicit capability reporting. This is substantially less work than manually speaking every screen, while making the limits visible to users.
