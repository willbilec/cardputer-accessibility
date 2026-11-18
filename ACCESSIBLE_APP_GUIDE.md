# Building an accessible Cardputer ADV app

When the user requests a Cardputer ADV app, implement the requested features
with keyboard operation and built-in offline speech by default. The app
request supplies the feature requirements. This guide supplies the
accessibility, integration and verification requirements. Apply them
automatically, including when the request only describes what the app should do.

Deliver working source, a build for the appropriate target, and text-first
controls the user can read with their desktop screen reader. Verify whatever
is possible without a board and report which interactions still need physical
listening and keyboard acceptance.

## 1. Choose how the app will run

- **An app inside the unified firmware.** Start here for a new tool alongside
  Keyboard learning, Voice recorder and File manager. The app is C++ code
  linked into the same image and uses its keyboard, speech and audio services.
- **A standalone firmware or an upstream port.** Give it its own speech and
  input handling. Use the [offline speech integration guide](TTS_FIRMWARE_GUIDE.md).
  The launcher stops running when control passes to a different firmware.
- **A MicroHydra app.** The C++ reader has no MicroPython binding. The timer
  experiment moved the countdown interaction into C++. Accessible speech in the
  original Python app would require a MicroHydra speech integration.

## 2. Describe the app in keyboard and speech terms

For each interactive screen, choose the keys, focus order, spoken feedback
and exit behavior before implementing it. Use the existing global commands
and menu conventions. Make the essential workflow usable from power-on
without looking at the display. For example, a counter would work as follows.

1. Enter on Counter from Home. Hear “Counter. Zero. Semicolon decreases.
   Period increases. Alt Q returns home.”
2. Press period. Hear “Count, one.” Press semicolon. Hear “Count, zero.”
   At the lower bound, hear “Minimum, zero.”
3. Press Option + R to repeat. Confirm the latest count is the repeatable
   announcement. Option + Space stops speech. Option + H reads shortcut help.
4. Press Alt + Q. Hear Home and the current selection. Reopen Counter and
   confirm the documented re-entry behavior.

Use this same format for errors, confirmation and asynchronous completion.
State the target and consequence before a destructive action. Announce changes
from app state, including selection, value and outcome. Keep focus stable on
redraw. Allow fast navigation to replace obsolete focus speech and give
important outcomes enough time to be heard. Keep password characters out of
default speech, repeat buffers and diagnostics. Keep ordinary remote typing silent.

## 3. Add the screen to unified firmware

Open [src/unified.cpp](research/offline-reader/src/unified.cpp) in your editor.
Keyboard learning is the smallest existing Home-to-app example.

1. Find the `Page` enum and add the new page. Find the Home labels and
   selection bounds. Update the label, count and activation together so the
   spoken position matches the item Enter actually opens.
2. In `handle()`, find the Home activation switch by searching for
   `case 0:page=Page::Learn`. Add a case for the new page, initialize its
   state, and call the existing `say()` from that transition. Keep demo
   state in the UI path.
3. Find `handle()`. Add page-specific behavior after the global shortcut
   handling. Follow existing position/event handling for key presses. Announce
   the new value after applying it. Check key release and modifier combinations.
4. Find `homePage()` and `loop()`. For apps that use resources, release
   files, pending work, timers and audio when leaving. Use short updates in
   the loop and preserve keyboard response during slow work. Use the shared
   mixer for audio. A counter needs no new driver or background task.
5. Update the user-facing keys in [unified controls](research/offline-reader/UNIFIED_CONTROLS.md).
   The installed development build has its own controls and limitations.

## 4. Build and check from PowerShell

Run these checks from the workspace root using the project's Python and
PlatformIO setup. Use the corresponding build target for standalone firmware.
The dependency preparation uses the local pinned BackgroundAudio checkout.

```powershell
Set-Location research/offline-reader
python prepare_background.py
python run_host_tests.py
python -m platformio run -e unified -j 6
```

Read the test summary and build result in your terminal. Run one PlatformIO
build at a time in this directory. The standalone image is
`.pio/build/unified/firmware.bin` under `research/offline-reader`. The
`unified-dev` environment adds USB diagnostic commands. Host tests cover
logic and fakes. A new screen still needs its own keyboard walkthrough.

## 5. Verify by listening on the device

Building does not install an image. Before an authorized install, verify the
actual device, image size, partition table, backups and recovery route. The
tested unit uses the existing 2.5 MiB application slot at `0x10000`.
See the [installation and recovery guidance](FUTURE_FIRMWARE_GUIDE.md#installation-and-recovery).

On the ADV, start at power-on and complete the entire app route without using
the display. Check re-entry, rapid key presses, Stop, Repeat, Help, cancellation,
errors and recovery. For a file or network app, test missing SD, connection
failure and a successful run. Repeat the relevant workload with Wi-Fi off,
connected and transferring. Listen through the speaker and headphones when
the workload uses audio. Record what the user heard and distinguish it from
software timing and host checks. See the [firmware verification checklist](FUTURE_FIRMWARE_GUIDE.md#verification).
