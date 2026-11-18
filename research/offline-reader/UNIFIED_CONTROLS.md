# Unified firmware controls (development version)

This firmware is under development. Controls and behavior still require
physical-device verification.

Use semicolon and period to move through a menu and Enter to select. Backspace
goes to the parent folder in the file manager. Alt Q leaves any program and
returns home. Function + backtick is Escape and goes back from settings and
input screens. On this keyboard, Function + semicolon and Function + period are
the physical up and down arrows.

In Home, Settings, Time zone, File manager, file actions, and the Wi-Fi
network list, hold up or down to move through multiple items. A single press
wraps at the ends. Holding a key stops at the end of the list. Comma jumps
to the first item and slash to the last. Space reads the current selection
and its position. Control stops speech, and Option + H reads menu help.

Global controls:

- Option + Function + semicolon / period: reader volume up / down.
- Option + semicolon / period: reader speed down / up.
- Alt + comma / slash: reader speed down / up, up to 450 words per minute.
- Alt + semicolon / period: reader and media volume up / down together.
  In PlayPalace this also adjusts effects volume.
- Option + Space: stop speech. Option + R: repeat. Option + H: shortcut help.
- Alt + Q: home. Alt + B: approximate battery. Alt + T: time. Alt + N: network.

Home contains Keyboard learning, Voice recorder, File manager, Bluetooth and USB
keyboard, Settings, and Browse SD card over USB or Wi-Fi.
Keyboard learning describes pressed keys without sending them to another
computer. In Voice recorder, Enter starts recording and Enter again saves it.
Space pauses or resumes recording or preview playback. A high beep signals
start or resume, and a low beep signals pause or the end of playback.
Backspace asks to delete the current recording or the most recent recording
from this session. Enter confirms and Backspace cancels. A recording paused
for confirmation stays paused after cancelling. Space resumes it.

Tap Control to start a gain-test preview, then tap it again to hear the
sample. Previews play automatically after about ten seconds if left running.
The app removes the temporary WAV after playback or on exit, and clears a
preview left by a reset when starting the next recording.

Both modes require an SD card. Saved WAVs stream to `/Recordings` through a
4 KiB buffer. The recorder syncs the audio and updates the WAV header about
once per second, and again on pause or save. A sudden power cut can still
lose audio after the last completed checkpoint.
Before starting a recording, semicolon and period raise and lower microphone
gain in 3 dB steps. The reference level is 12 dB louder than the previous
zero-gain recording path. Set gain to minus 12 dB for the previous reference
level. This affects new recordings. Microphone gain and media volume are also
in Settings.

To hear a saved recording, open its WAV from the File manager. Playback
uses the same beeps for start, pause, resume and finish. Backspace stops
playback and asks for spoken confirmation before deleting the file.

Settings opens Network, Date and time, Audio, Recording, and Storage. Use
semicolon or period to move, Enter to open a category, and Backspace or
Function + backtick to go back one menu level.

In Storage, select Internal flash space to hear the chip capacity, running
firmware size, and room left in its firmware partition. Select SD card space to
hear the mounted filesystem capacity, used space, and available space.

Alt + B reads the battery estimate in five-point increments. With a USB
data host connected or a voltage near the top of the range, it reads voltage
and prompts you to unplug. A wall charger can still raise the estimate because
the board measures battery voltage and has no charging-status readout.

The file manager lists the Bluetooth keyboard/mouse program and the current SD
folder. Enter opens the selected item; M opens file actions. The action menu
provides rename, delete, copy, paste and folder creation. Delete requires Enter
to confirm and removes empty folders only. Copying an individual file can be
cancelled with Alt Q; Space announces its progress. Typing a new name echoes
characters, Backspace deletes, and Function + backtick cancels.
Use Refresh SD card in the action menu after inserting or replacing a card.

Open a WAV or MP3 file to play it. Space pauses or resumes; semicolon and period
raise and lower media volume independently of the reader; comma and slash
seek backward and forward; Backspace returns to the file list. WAV seeking is
available for 44.1 kHz, 16-bit stereo PCM, and MP3 seeking is approximate.
Use Alt Q to stop playback and go home.

Open Bluetooth and USB keyboard from Home or File manager. When USB is already
connected, the app starts in USB mode. Alt + 4 selects USB at any time in the
app. Tap Alt + 1, 2 or 3 to reconnect to that Bluetooth slot. To pair, hold Alt
and the slot number until you hear the instructions, then choose the matching
**Cardputer KB 1**, **Cardputer KB 2**, or **Cardputer KB 3** entry, followed by
your Cardputer's four-character suffix. Each slot has its own stable Bluetooth
address. Devices paired to the former shared name need to be paired once again.
The keyboard sends reports to one host at a time. A high cue means connected and
a low cue means disconnected. Option + M switches between keyboard and mouse.
Remote typing is silent. The keyboard app stops Wi-Fi and uses audio cues
while Bluetooth needs the reader's memory. Returning Home stops Bluetooth and
restores speech.

On `unified-playpalace`, Browse SD card over USB or Wi-Fi opens three modes. Press period to move from
USB read only to USB read and write and then Wi-Fi read and write. Press Enter
to start sharing, Space to repeat connection information, and Alt + Q to end
the session and cancel incomplete transfers. The settings folder can contain
saved Wi-Fi credentials. The 2.5 MiB `unified` build keeps the two USB modes.

### Windows two-pane browser

Run these commands from the offline-reader directory.

```powershell
python -m pip install wxPython pyserial
python sd_browser_gui.py
```

For USB, connect a data cable, choose USB in the app, then press Connect. Leave
the COM port blank to detect one attached Cardputer automatically. Use the
current firmware for the GUI's binary transfers and file operations.

For LAN, install firmware that includes Wi-Fi SD sharing. Put the Cardputer and
PC on the same local network. Start Wi-Fi read and write on the Cardputer.
Choose Wi-Fi in the PC app and use Find Cardputers or type the spoken IPv4
address. Enter the eight-digit sharing code and press Connect. Press Space on
the Cardputer to hear its address and code again. If discovery is blocked, try
the address directly. Both UDP discovery and TCP transfer use port 47631.

The left list is the Cardputer SD card and the right list is the local computer.
The connection controls are hidden while connected. Use Disconnect to show
them again. Use Tab to reach controls, F6 to switch file lists, and arrows to
select items. Enter opens a folder or Parent folder. On a local file, Enter
uploads it into the open SD folder. On an SD file, Enter downloads it into
the open local folder. Ctrl+U and Ctrl+D also work from elsewhere in the
window. To open a path directly, type it into the path field and press Enter.

Press Shift+F10 or the Applications key on an item for its context menu.
Both panes support rename and confirmed deletion. Folders must be empty
before deletion. Use New SD folder to create an SD folder and F5 to refresh.
Existing files are protected from overwrite.

The Transfer progress bar tracks uploads and downloads. Ctrl+P focuses the
bar. Ctrl+I focuses the status field with percentage, bytes transferred,
total bytes, and average speed.

Wi-Fi shares the Music, Recordings, and Transfers folders and creates Transfers
when possible. Browse into a shared folder before uploading. Sharing uses a
per-session code and sends data in plaintext on the LAN. Use USB read and write
to provision credential files directly in an application's folder.

The USB command-line helper remains available. The helper uses pyserial
and detects the Cardputer's COM port. Add `--port COM6` before the command to
select a port explicitly.

```powershell
python usb_sd_browser.py ls
python usb_sd_browser.py ls /Recordings
python usb_sd_browser.py get /Recordings/recording-00001.wav
python usb_sd_browser.py put "C:\Music\test.mp3" /test.mp3
python usb_sd_browser.py sync-time
```

Settings contains Wi-Fi scanning and connection, disconnect, network time,
time zone, typed and menu-driven time entry, speech rate and speech volume.
Wi-Fi password characters are spoken as typed, including capitals, with the
characters omitted from diagnostics. To type capitals, hold Shift, immediately
to the right of Function. Settings are stored on the SD card and mirrored
in flash. For typed time entry, enter twelve digits in year, month, day, hour
and minute order using a 24-hour clock. The menu-driven option lets you choose
each field with semicolon and period and confirm it with Enter. A saved
timestamp is announced as a reference after complete power loss. Use the
network or USB helper to set the current clock. Alberta Time uses UTC−6
year-round. U.S. Mountain Time remains available.

Entering Home shuts down Wi-Fi. A network time request can reconnect to a saved
network while automatic Wi-Fi is enabled. In the network list, Enter uses the
saved password and Function + Enter lets you change it.

## Synthesizer

From Home, press slash to select Synthesizer, then Enter. Every non-modifier
key plays, including punctuation, Tab, Backspace, Enter, and Space. The keys
rise in semitones from left to right, starting on Z in the bottom row and
continuing at the left of each higher row. Play up to eight notes together.

- Alt+1 selects Piano, Alt+2 Chiptune, Alt+3 Drums, Alt+4 Bass, and Alt+5 Glass
  Bells. Alt+6 selects Grand Piano and Alt+7 selects NES Chip, with stepped
  triangle bass and pulse leads. Alt+8 selects Analog Brass, Alt+9 Electric
  Piano, and Alt+0 Drawbar Organ. Each change is spoken. Drums cycle through
  twelve percussion sounds across the keys, starting with kick on Z. The
  added sounds are a high tom, cowbell, rimshot, and ride cymbal.
- Hold Ctrl to sustain released notes. Hold Shift for flanger and Fn for
  tremolo. Hold Option while playing for an arpeggio that pulses through
  the root, octave, and fifth above the octave. You can combine these effects.
- Alt+[ lowers Quiet Sound Boost and Alt+] raises it. The spoken
  setting ranges from 0 (off) to 8 (maximum) and is saved.
- Alt+H speaks the synth controls and Alt+R announces the current preset.
  Option+H and Option+R play notes with the arpeggio. Option+Space stops speech.
  Alt+semicolon and Alt+period adjust speech and synth volume together.
  Alt+Q stops the synth and returns to Home.

## PlayPalace

The `unified-playpalace` build adds PlayPalace after Browse SD card over USB or Wi-Fi.
It uses the expanded 8 MiB partition layout in `partitions-playpalace.csv`.

- Connect to Wi-Fi from Cardputer Settings. In PlayPalace, use semicolon to
  move up, period to move down, and Enter to select.
- Choose **Sign in** to enter a server URL, username, and password. The reader
  keeps password typing silent. A successful sign-in saves the login on SD.
  To use a desktop export, place it in `/PlayPalace/identities-export.json`,
  choose **Import desktop identities**, then select the account under **Saved
  accounts**. Control+Backspace removes the selected saved login.
- Press Enter at an unencrypted connection prompt to approve reconnects for
  that saved account and server. Alt+Q returns
  to Cardputer Home. Reopen PlayPalace to sign in to the same account and let
  the server recover your table. Log out followed by Yes returns to Home.
  Alt+S also exits PlayPalace, and Backspace exits from its sign-in screen.
  These exits keep the saved account. Use the game's Leave Table command to
  leave the table explicitly.
- Tab moves through Game, Chat, and History. Shift+Tab moves backward. In
  Game, semicolon is Up, period is Down, comma is Left, and slash is Right.
  Backtick, the top-left key, follows the desktop Escape action for the current
  game menu. Backspace also goes back when the menu supports it.
  Hold an arrow to repeat through the game menu. Fn + Alt + an arrow jumps to a grid edge.
  Fn with the number row invokes F-keys. Alt+M focuses Game. Alt+H reads help.
- Type a table message and press Enter to send. Prefix a message with a
  period and a space for global chat. In History, semicolon and comma read
  older lines, period reads newer lines, brackets select buffers, and F4 toggles the selected buffer's
  mute. Ordinary typing speaks key names. PlayPalace passwords stay silent.
  F6 toggles table chat and Shift+F6 toggles global chat.
- Server speech reads the full packet in order. A newer speech packet can
  interrupt a score or status read-through. Control (bottom-left) or Option+Space stops speech.
- A new game menu takes priority over ordinary focus and chat speech. PlayPalace
  disables Wi-Fi modem sleep while connected for faster replies, which can
  increase battery use during play.
- The global Media Volume caps PlayPalace effects. F9 and F10 adjust their
  level within that limit. Alt+U sets the HTTP or HTTPS sound asset
  base URL for the active account. Complete effects are cached in
  `/PlayPalace/sounds` for subsequent playback. Music and looping ambience
  are deferred. The firmware accepts Ogg Vorbis and 16-bit PCM WAV effects
  and skips network effects when heap is low. The optional
  `/PlayPalace/effects.ppfx` bank plays short IMA ADPCM effects straight from SD
  and works with the smaller connected heap. Menu movement speaks the focused
  item without an added click. Each new activation or gameplay effect replaces the previous effect.

## Build and installation

`python -m platformio run -e unified-playpalace -j 6` builds the app installed
on the development unit. Its 7.875 MiB reader partition starts at `0x10000`.
The November 18 migration wrote the generated partition table at `0x8000`
and the application at `0x10000`. Future updates for this layout can use the
verified app-only `firmware.bin`. The `unified` build still targets the older
2.5 MiB layout. Check the actual device layout before flashing either build.

WAV and MP3 decoder objects start on demand and are released when playback ends
or is cancelled. The recorder, file copy, and playback share a scratch buffer.
Bluetooth stops advertising, disconnects, and releases the BLE stack on exit.
Alt + B estimates battery level from the median of 15 voltage samples and a
one-cell lithium-ion curve. The percentage remains approximate while charging.
