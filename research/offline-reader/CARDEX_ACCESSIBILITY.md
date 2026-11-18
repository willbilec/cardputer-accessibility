# Accessible CardEX community-app experiment

2026-11-04. **The actual community app runs with spoken state and desktop-keyboard input in Velxio.** After the later buffer/chunk fixes, the user independently operated it in Microsoft Edge and reported that it all seemed to work pretty well. Help reached “Any other key closes help.” Earlier brief pauses and the buffer experiments below are historical observations. A perceived Stop delay included uncertain browser/control timing and does not establish end-to-end latency. No physical Cardputer has been tested.

The final user-tested image is `test-results/cardex/cardex-access-simulator.bin`, SHA-256 `bb8cb761fc13b6c3067c8d4cc9a427ca3a45d7966edceb55d6bd6fed81b72733`, application 1,978,016 bytes, requesting two output buffers. File selection, opening, line navigation, character entry, undo, save, closing, settings and recursive search were exercised. This is positive acceptance of the tested emulator flows, not all app functions or physical performance.

## App and scope

[CardEX](https://github.com/prokke/CardEX-for-Cardputer-ADV) is a community file manager and text editor with multi-selection, recursive search, file operations, bookmarks, configurable settings, undo/redo and viewers. The local checkout starts at `87506624e3e2e0e4bc8e1cb2770b56e94372d185`. Its MIT notices are retained. The speech dependency remains BackgroundAudio 1.4.4, pinned and guarded as described in [mixed-audio results](COEXISTENCE_RESULTS.md).

This modifies the actual C++ community application, rather than replacing it with a menu mock. File operations, editor state, search and settings use the original app's implementation. New `Reader` calls announce semantic focus and operations; they do not scrape pixels or draw calls. This is a per-app accessibility adaptation, not a universal screen reader for arbitrary firmware.

The simulator profile bypasses physical LCD transfers, cosmetic RGB LED timing and USB mass storage. An image-viewer command announces its unavailability and returns to the file list. The app still uses an off-screen canvas for its original rendering code; this is not a claim that all graphics memory has been removed. USB mass-storage commands also announce the simulator limitation instead of trying unsupported hardware. The normal upstream build has these exclusions guarded behind the emulator profile.

## Keyboard controls

Velxio [documents keyboard input](https://velxio.dev/docs/boards/reference/cardputer-adv/): focus the board once, then type on the computer keyboard. With a desktop screen reader, use focus/forms mode so the keystrokes reach the board. Click keycaps is another emulator path, but it does not establish keyboard accessibility of Velxio's surrounding controls.

The accessible build accepts **Ctrl as an additional Cardputer Fn modifier**, so a desktop keyboard can reach the app's commands. The physical Fn key remains supported.

| Keys | Action |
|---|---|
| Semicolon / period | Up / down in files; previous / next line in editor |
| Comma / slash | Parent / open in files; left / right in editor |
| Enter | Open selected item; insert newline in editor; accept a dialog |
| Backspace | Parent folder; delete previous character in editor |
| Space | Toggle file selection |
| Ctrl+Space | Read current context again |
| Ctrl+Shift+Space | Read the next 280-character context chunk; wraps at the end |
| Ctrl+Backspace | Stop speech |
| Ctrl+H | Context-sensitive spoken help |
| Ctrl+O | Settings; semicolon/period move, comma/slash change |
| Ctrl+N | Create a file or directory |
| Ctrl+F | Search files; find text in editor |
| Ctrl+P | File properties |
| Ctrl+C / X / V | Copy / cut / paste in file manager |
| Ctrl+D / R | Delete / rename in file manager |
| Ctrl+S / Q | Save / close editor |
| Ctrl+Z / Y | Undo / redo in editor |

Escape on the Cardputer is Fn+backtick. **Desktop Ctrl+backtick was verified to exit settings.** Native desktop Escape mapping was not verified. Dialogs speak their accept/cancel choices. Confirmation dialogs also accept Y/N, and Backspace cancels them.

## Exercise data and speech behavior

When `/reader-demo` does not already exist, this simulator-only profile creates it with `Welcome.txt`, `Shopping.txt` and `Projects/Plan.txt`, then opens that directory. It never overwrites an existing demo directory. Files edited here belong to the emulator's virtual SD, not a physical card.

File focus includes name, type, size, position, multi-selection and bookmark state. Settings speak the selected label and value. Prompts, confirmations, messages, toasts and coarse progress updates speak explicitly. The editor announces line/column, changed state, typed characters and movement; repeat-context reads the full current line in chunks. The text pager and hex viewer expose their current page through speech and the same repeat/more commands. Image content is not described.

Critical announcements defer automatic focus speech until completion. Latest focus replaces older pending focus, avoiding a growing navigation backlog. Explicit repeat or Stop can interrupt. Sensitive input prompts suppress automatic character speech. This is a fixed-English prototype, with no accessibility tree or MicroPython binding.

## Startup issues and reproduction

The first simulator build failed during TinyUSB initialization; the emulator profile now uses hardware CDC serial. Another run repeatedly rebooted after file-manager initialization. Decoding its return address against the matching ELF located the call in `UI::pushCanvas()` → `LGFXBase::pushImage`, with a null program counter. Bypassing LCD transfer and explicitly selecting the ADV fallback board are corrective changes under test, not proof of a physical-device defect. RGB LED initialization is also excluded from this profile.

The corrected profile reached `Ready!` and spoke the directory without restarting. Keyboard presses then accumulated in the emulated TCA8418 FIFO (`pending=2`, IRQ level 0), but the vendor reader's interrupt flag did not cause it to consume them. This profile injects a polling reader and explicitly services it in main and modal input loops. It consumes one event per frame so a press and release cannot disappear between application snapshots. The 56-key matrix mapping, press/release preservation, duplicate presses and malformed input pass a host test against the actual polling-reader class, with address/undefined-behavior sanitizers. Those fakes do not validate physical I²C or IRQ behavior.

Speech codec initialization now shares `M5.In_I2C` with the keyboard instead of opening a second I²C interface on the same pins. The polling driver, shared bus and board selection are simulator integration changes; the successful keyboard checks below are actual emulated peripheral input, not direct calls into the app's menu handlers.

## Observed keyboard paths

| Input | Firmware outcome / spoken state |
|---|---|
| Period | `CARDEX_KEY code=46`; Shopping.txt, file, 2 of 3 |
| Enter | Shopping.txt editor, line 1 of 4, column 1; Shopping list |
| Period in editor | Line 2; Apples |
| `x` | Character echo; editor modified |
| Ctrl+Z | Undo restores Apples |
| Ctrl+S | File saved; editor reports Saved |
| Ctrl+Q | Returns to file selection |
| Ctrl+O, then period | Settings Background, then Text, with values and positions out of 38 |
| Ctrl+backtick | Returns from settings to file manager |
| Ctrl+F, type `plan`, Enter | Spoken prompt and query updates; one result, Projects/Plan.txt |

These checks used the focused `velxio-cardputer-adv` keyboard surface and real key events. The inserted `x` was undone before saving, restoring the demo shopping list. Redo, destructive operations, all 38 setting values, large-file pager, hex navigation and long editing sessions have not received equivalent live acceptance testing.

The first interactive image was `ba0c5ee4d6022eace6aa00b329d5838927b222d6235a285f327cf33d0dd01dba`. The buffer/performance revision is `6bef608c033a8e85d95e98e66d2439b8d6c85b509b186e2b170ccaba31806b96`, 1,977,616 application bytes. It requests three output buffer frames, rather than one; the ESP32 driver may split each into smaller DMA buffers. About 180 ms of audio capacity is a configuration calculation, not a measured audible interruption delay. Idle focus construction and unchanged editor text work have also been reduced. Current serial statistics after 50 simulated seconds report zero output underruns, 125,128 bytes free and 110,712 minimum; listening feedback remains necessary to assess the brief pauses.

Run `python emulation/build_cardex_trial.py` from `research/offline-reader`. It builds the modified checkout with `platformio-access.ini`, captures its source hashes, and creates `test-results/cardex/cardex-access-simulator.bin`. Upload it with File → Upload firmware, then Run. This image has a dedicated simulator-only partition table and must not be treated as a physical installation/migration image. It has not been added to the SD firmware catalogue.
