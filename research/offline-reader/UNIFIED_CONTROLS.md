# Unified firmware controls (development version)

This firmware is under development. Controls and behavior still require
physical-device verification.

## Bible

Choose Bible from Home. Place the NLT files under `/bible/NLT bible` on the
SD card. The app discovers version folders inside `/bible`. Each version
uses numbered book folders and chapter MP3s named like
`Faith Comes By Hearing - Genesis 1.mp3`.

- B opens the book list. Use Up and Down (semicolon and period) to move,
  or hold either key to skim. Press the first letter or digit of a book's
  name to jump to it. Press the same key again to move to another match.
- Enter on a book opens its chapter list. C opens the current book's chapter
  list from the player. In the list, 5 jumps to the first available chapter
  in the 50s, or chapter 5 if the book has no chapters in the 50s. Other
  digits work the same way.
- Enter starts the selected chapter. Space reads the selection. Backspace
  returns to the previous menu.
- Control locks or unlocks the keyboard. Playback continues while locked.
  Minus and Equals lower and raise media volume by 5 percentage points
  when the keyboard is unlocked.
- Space plays or pauses. Semicolon (Up) moves to the next chapter and
  period (Down) goes to the previous chapter. Chapters also advance
  automatically, including into the next available book. Automatic
  transitions leave the title announcement to the recording.
- Comma (Left) and slash (Right) seek backward or forward and look for a
  quiet gap near the target. The offset is approximate. While paused,
  the search runs when playback resumes.
- Left bracket slows playback and right bracket speeds it up by 5 percentage
  points, from 50 to 135 percent. The app announces and saves the speed.
  Playback speed changes preserve pitch.
- M saves one bookmark. Alt + M plays from the bookmark. The app also
  saves a separate resume position on exit and resumes it when reopened.
- V lists available versions or announces the only installed version.
  Switching versions opens the same chapter from the beginning if available.
- Option + H reads help. Alt + Q returns Home.

The app finds 3 John 1 in the John folder and presents it under 3 John.

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
- Alt + comma / slash: reader speed down / up.
- Alt + semicolon / period: reader and media volume up / down together.
  In PlayPalace this also adjusts effects volume.
- Option + Space: stop speech. Option + R: repeat. Option + H: shortcut help.
  The standard unified reader supports rates up to 650. Rates through 450
  use native eSpeak timing. Above 450, the reader compresses the generated
  audio in time while preserving its pitch. NVDA Remote uses native eSpeak
  timing during a connection and restores the saved boost when you leave.
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

Open a TXT file to read it with offline speech. Enter or period reads the next
section. Semicolon goes back through the last 64 sections, Space rereads the
current section, and comma starts over. Option H reads the controls. Backspace
returns to the file list. Text files can be ASCII or UTF-8.

Open a WAV, MP3, or Ogg Vorbis file to play it. Ogg Vorbis files can use the
`.ogg` or `.aug` extension. Space pauses or resumes. Semicolon raises media
volume and period lowers it. Comma and slash seek backward and forward in MP3
and 44.1 kHz, 16-bit stereo PCM WAV. MP3 seeking is approximate. Backspace
returns to the file list. Alt Q stops playback and goes home.
While playing MP3, Ogg or AUG, Alt F toggles the bass cut and announces the
saved setting. The bass cut starts enabled to reduce low-frequency strain on
the built-in speaker. Turn it off for full bass through the 3.5 mm jack.
Alt L toggles Quiet Boost, which starts enabled and raises quieter passages
without raising the music peak ceiling. It can be turned off independently
for the track's original dynamics.
Plugging in headphones automatically mutes the built-in speaker. Lower both
speech and media volume before plugging in headphones, then adjust to taste.
These controls apply to the `unified` firmware, which fits in the single
application partition defined by `partitions-playpalace.csv`.

Open Bluetooth and USB keyboard from Home or File manager. The app tries the
last Bluetooth slot that connected successfully. Alt + 4 selects USB when a USB
data host is connected. Tap Alt + 1, 2 or 3 to reconnect to that Bluetooth slot. To pair, hold Alt
and the slot number until you hear the instructions, then choose the matching
**Cardputer KB 1**, **Cardputer KB 2**, or **Cardputer KB 3** entry, followed by
your Cardputer's four-character suffix. Each slot has its own stable Bluetooth
address. Devices paired to the former shared name need to be paired once again.
The keyboard sends reports to one host at a time. A high cue means connected and
a low cue means disconnected. Option + M switches between keyboard and mouse.
Remote typing is silent. The keyboard app stops Wi-Fi and uses audio cues
while Bluetooth needs the reader's memory. Returning Home stops Bluetooth and
restores speech.

Tap Alt by itself to switch between the regular and alternate keyboard layers.
The alternate defaults are Semicolon for Up, Comma for Left, Period for Down,
Slash for Right, the top-left Backtick / Tilde key for Escape, and Backslash
for Windows. Option defaults to Windows (GUI)
in both layers. Tap Option to send Windows alone or hold it with a remote key.
Option + M and the shared Option reader shortcuts still run locally. Each
Bluetooth slot and USB remember their keyboard layer and keyboard or mouse mode.
The high beep confirms the alternate layer and the low beep confirms the
regular layer over Bluetooth or USB.

Open Settings, Keyboard, Key Map Manager to remap either layer. Tap Alt to pick
the layer. Press a physical key to hear its current assignment. Hold it for
about half a second, or tap it and press Function + Space, to open its choices.
Type directly to search across categories. Backspace edits the search.
Function + Comma and Slash browse categories. Function + Semicolon and Period
browse results. Enter assigns the spoken result and Function + Backtick cancels
the picker. Choose Default assignment to restore that layer's original behavior
or Disabled to suppress the key. Alt + Q saves and exits to Keyboard settings.
Alt and Function are reserved controls. Function shortcuts keep their usual
assignments. The same saved maps apply to Bluetooth and USB.

In the standard unified firmware, Browse SD card over USB or Wi-Fi opens three modes. Press period to move from
USB read only to USB read and write and then Wi-Fi read and write. Press Enter
to start sharing, Space to repeat connection information, and Alt + Q to end
the session and cancel incomplete transfers. The settings folder can contain
saved Wi-Fi credentials.

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
When an upload or download finds an existing file with the same name, the
browser asks before replacing it. The original stays in place during the
transfer. To edit `/nvda-remote.ini` through the browser, choose USB read and
write on the Cardputer, download the file, edit it locally, then upload it to
the SD root and confirm replacement.

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

## Voice changer

From Home, press Slash to select Synthesizer, then Semicolon once to
select Voice changer. Press Enter to open it.

| Key | Action |
| --- | --- |
| 1 through 0 | Select Robot, Flanger, Deep voice, High voice, Echo, Tremolo, Radio, Fuzz, Chorus or Whisper. The reader announces the selected voice. |
| Enter | Start recording. Speak after the beep. |
| Any key while recording | Stop recording and play through the selected effect. Playback also starts automatically when the clip reaches its time limit. |
| Space or Apostrophe, immediately left of Enter | Replay the last clip with the currently selected effect. |
| M | Start or stop live monitoring. Use headphones to avoid speaker feedback. |
| Alt + left or right bracket | Decrease or increase the shared microphone gain in 3 dB steps. Voice recorder and Recording settings use this same setting. |
| Alt + H or Option + H | Hear the controls. |
| Alt + Backspace | Stop playback or monitoring. The clip remains available for replay. |
| Backspace or Alt + Q | Return to Home and discard the clip. |

The clip stays in RAM. Starting another recording replaces it. The recording
limit is four to twelve seconds, depending on free memory. Choose the effect
and microphone gain before starting a recording. Speech and media volume
controls retain their usual shortcuts.

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
- Alt+Z, Alt+X, Alt+C, Alt+V, and Alt+M start backing loops 1 through 5
  for the current preset. Play the keyboard and use effects over the loop.
  Another loop shortcut replaces the current loop, and the same shortcut
  restarts it. Alt+Backspace or Alt+Tilde stops the loop. Changing presets
  also stops it.
- Alt+H speaks the synth controls and Alt+R announces the current preset.
  Alt+Space stops speech. Alt+comma and Alt+slash adjust speech rate.
  Alt+semicolon and Alt+period adjust speech and synth volume together. Add
  Fn to those volume chords to adjust speech volume alone. Alt with Fn and
  the left or right arrow also adjusts speech rate. Holding Option while
  playing the arrow keys adds arpeggio. Alt+Q stops the synth and returns
  to Home.

## PlayPalace

PlayPalace is on the standard unified Home menu after Browse SD card over USB or Wi-Fi.
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
  Tap Fn by itself to read the game view's information, such as available credits during
  a Space Base purchase, without moving focus.
- Type a table message and press Enter to send. Prefix a message with a
  period and a space for global chat. In History, semicolon and comma read
  older lines, period reads newer lines, brackets select buffers, and F4 toggles the selected buffer's
  mute. Ordinary typing speaks key names. PlayPalace passwords stay silent.
  F6 toggles table chat and Shift+F6 toggles global chat.
- History, score, and status read-throughs complete before new server
  announcements play. Press a key to interrupt a read-through. Control
  (bottom-left) or Option+Space stops speech.
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
  item without an added click. Enter plays its confirmation cue from the SD
  effects bank when available. New gameplay effects replace previous effects.

## NVDA Remote

On the Windows computer, open NVDA's Tools menu, choose Remote Access,
and set up a session that allows that computer to be controlled. You can
use a relay or host the session on the computer. The Cardputer uses the
saved Wi-Fi network from Settings.

1. Open **NVDA Remote** from Cardputer Home. Use Semicolon and Period to
   choose a recent connection, **Load from SD**, or **Enter manually**.
   Function+Semicolon and Function+Period also work, as do Shift+Tab and Tab.
2. **Load from SD** creates a blank `nvda-remote.ini` template at the root
   of the SD card if the file is missing. Use a plain UTF-8 file with each value immediately
   after its equals sign. The file contains the session key in plain text.
   For manual entry, press Enter after each field. Enter a host without the
   port or a URL scheme, then the port, the key, an optional certificate
   fingerprint, an optional connection name, and an optional Wake-on-LAN
   MAC address. Leave the port blank for 6837. Use Backspace to
   edit and Function+Backtick to go back. The session key stays silent.
3. Leave the fingerprint blank to use normal certificate validation. If the
   Cardputer prompts you to trust a certificate, press **Alt+F** to hear its
   SHA-256 fingerprint. For an NVDA direct host, compare it with
   `%APPDATA%\nvda\remoteAccess\localRelay\NvdaRemoteRelay.fingerprint` on
   the Windows host. **Alt+Y** trusts and saves the fingerprint. **Alt+O**
   trusts it for the current session. **Function+Backtick** cancels. You can
   also enter the verified fingerprint in advance as 64 hexadecimal digits
   with optional colon separators.
4. Review the host and port and press Enter. After a new connection,
   press **Alt+Enter** when the Cardputer announces one controlled computer.
   Selecting a recent connection starts forwarding automatically once that
   computer is ready and the keys used to select it have been released.
   The Cardputer reads incoming NVDA speech using its offline eSpeak voice.
   During rapid navigation, the newest announcement can replace older
   updates, including multiple updates for the same keypress.
   Uninterrupted read-through stays in order.
   A remote pause stops the current segment. On resume, the segment starts
   again from its beginning.

Up to five recent connections appear first, newest first. A valid SD import
is saved immediately. A manual entry is saved when a controlled computer is
available. Press Enter to reconnect or Alt+Backspace to forget a selected
connection. Recent connections store keys in the Cardputer's internal settings
and the SD import file remains available for editing. Use an optional
connection name to distinguish computers on the same relay.

### Wake-on-LAN

Add `wake_mac=` with the MAC address of the wired or wireless adapter
that stays available during sleep. To update an existing recent connection,
`nvda-remote.ini` can contain just `host`, `port`, and `wake_mac`. **Load
from SD** matches that host and port to one saved profile and keeps its
session key and certificate information.

If a saved computer is unreachable at connection time, NVDA Remote sends
one wake packet on the local network, waits eight seconds, and retries.
For wake without connecting, open **Wake a saved computer** from the app
menu, use Semicolon or Period to select a profile, then press Enter.
Press Backspace to return to the NVDA Remote menu.
The wake announcement confirms the packet was sent. Actual wake depends
on the PC and network.

The app starts on the alternate keyboard layer. With the default mappings,
Semicolon, Comma, Period and Slash send Up, Left, Down and Right. Tap the
physical Alt key to switch layers while forwarding. Backslash types backslash
on both default layers. Option sends the Windows key. Custom mappings still apply.

- Hold **Fn** and press a key such as **N** to send **NVDA+N** and open
  the remote NVDA menu. Fn uses Caps Lock, which must be enabled
  under **Select NVDA Modifier Keys** on the remote computer.
- **Fn+1** through **Fn+Equals** send **F1** through **F12** when started
  with a fresh Fn hold. **Fn+Backtick** sends Escape and **Fn+Backspace**
  sends Delete.
- For NVDA commands on the number row, hold **Fn+physical Alt**, then press
  the number. For an NVDA command with an F key, press **Fn+physical Alt**,
  release physical Alt while keeping Fn held, then press the F key's top-row
  key.
- Hold **Control+Option** to send Windows Left Alt. For example, hold both
  keys and press **Tab** for Alt+Tab.

Global reader shortcuts stay local. Use **Alt+Enter** to suspend
forwarding, **Alt+D** in local mode to disconnect, or **Alt+Q** to return
Home. **Alt+H** reads the app controls in local mode. After a connection loss,
press Enter to retry. A stalled keyboard connection discards queued input
before closing. An explicit retry from a recent connection starts forwarding
once the connection is ready again.

## Build and installation

The [builder](../../BUILDING.md) selects the installed apps. Its 7.875 MiB
reader partition starts at `0x10000`. Verify the installed table before
an app-only update. See [flashing and recovery](../../FLASHING.md).

WAV, MP3 and Vorbis decoder objects start on demand and are released when playback ends
or is cancelled. The recorder, file copy, and playback share a scratch buffer.
Bluetooth stops advertising, disconnects, and releases the BLE stack on exit.
Alt + B estimates battery level from the median of 15 voltage samples and a
one-cell lithium-ion curve. The percentage remains approximate while charging.
