# Unified accessible Cardputer ADV firmware: design and implementation status

This document preserves the initial design and early device observations.
For the later implemented controls and verification, see [unified controls](UNIFIED_CONTROLS.md)
and [project status](../../PROJECT_STATUS.md). For new implementations, follow
the [future firmware guide](../../FUTURE_FIRMWARE_GUIDE.md).

Status: an incomplete unified development image has been flashed to the
existing factory application slot for device testing, but is not a release.
The original partition table and SD contents were retained. `src/unified.cpp` contains an
initial common speech, keyboard, WAV/MP3, Bluetooth, SD file operations,
settings and a duplex microphone recorder. None of these integrated paths has
been accepted on a device. Runtime memory, microphone capture quality and
Bluetooth/Wi-Fi coexistence are unknown. An initial user test experienced a
restart. A subsequent status report showed USB as the last reset reason, and
serial-port connections occurred during the test; the cause is not yet proven.
Do not treat the development image as a verified release.

The user subsequently reported keyboard learning remained responsive for about
30 seconds with serial connections left alone and confirmed Alt+Q returns home.
This does not prove the earlier reset's cause or validate the other programs.
Serial monitoring must not repeatedly open a port with default DTR/RTS enabled:
pySerial uses both by default, and that is a plausible source of USB resets.

Build with `python -m platformio run -e unified-dev -j 6` in this directory.
The single factory
application partition in `partitions-unified.csv` is a candidate, not a tested
installation or recovery layout. Preserve the SD card and existing diagnostic.

## Goal and architecture

Build one ESP32-S3 firmware image, booting directly to its home screen. It owns
one speech synthesizer, one audio output and mixer, one input dispatcher and one
set of persistent settings. Applications are components within the image, not
separate firmware boots. They expose semantic focus, value, state and event
messages to the shared screen reader; this is not automatic interpretation of
arbitrary third-party graphical applications. Adapt or rewrite the existing
applications as necessary. Each application has entry, input, update and exit
hooks and releases files, audio, Bluetooth reports and other resources on exit.

Input is centrally dispatched in this order: update physical key state; detect
global shortcuts; consume recognized shortcuts locally; then send remaining
events to the foreground app. Never send locally consumed keys to a Bluetooth
host. On entering and exiting HID mode send all-keys-released and wait for
physical release before resuming reports. Provide a documented way to send
host shortcuts that conflict with local commands; its exact interaction needs
design and physical testing. Sensitive input must not announce password
characters or log credentials. Ordinary remote HID typing remains silent
because the host may be accepting a password.

Speech should use the physically tested BackgroundAudio/eSpeak setup, with a
bounded queue, priority and interruption policy, stop/repeat controls, adjustable
rate and independent speech/media levels. Frequent selection changes can replace
stale selection announcements; errors and important progress must not be lost
to redraws. The shared mixer must manage recording, playback and speech without
multiple components taking ownership of the codec. Verify whether the ADV can
record and speak simultaneously with the chosen codec configuration; if not,
announce recording start before capture and report its state at safe boundaries.
Never claim recording works until actual microphone data and saved files are
validated on the device.

## Screens and services

- Home: Keyboard learning, Voice recorder, File manager and Settings. Navigation,
  activation, disabled/error state and position are spoken.
- Keyboard learning (keyboard helper): describe physical keys and modifier combinations without
  sending them to another device. Expose help and a way home.
- Voice recorder: create uniquely named recordings on SD; accessible start,
  stop, saved filename, elapsed state and errors. Handle missing/full/removed SD
  and interrupted writes; finalize a playable file where possible. Offer a
  recording list through the file manager. Microphone format and the amount of
  simultaneous speech require target measurements.
- File manager: browse SD directories, speak type/name/position, open supported
  files, and provide accessible operations with explicit destructive-action
  confirmation. A media player is opened for supported audio files; playback,
  pause, seek, stop and errors must be accessible. The Bluetooth keyboard/mouse
  program is also available from the file manager as an application entry;
  starting it must not require reflashing. Announce connection and mode changes.
- Settings: Wi-Fi scan/connect/disconnect and status; time configuration and
  synchronization with explicit time-zone and unavailable-time handling; speech
  rate and volume. Persist settings intentionally and do not speak passwords.
  Report real device battery readings, with an honest unavailable state where a
  trustworthy level cannot be obtained. Do not invent an RTC or battery gauge.

## Global commands

On this keyboard, arrow up and down are Function + semicolon and Function +
period. These combinations are distinct from the unshifted punctuation:

| Keys | Action |
| --- | --- |
| Option + Function + semicolon / period | Reader volume up / down |
| Option + semicolon / period | Speech rate down / up |
| Alt + Q | Close current application and return home |
| Alt + B | Battery status |
| Alt + T | Time, or unavailable if not set |
| Alt + N | Network status |

The initial position-based router is `include/global_shortcuts.h`; it is not
yet connected to a running image. It consumes only the exact modifiers listed,
and key-down events only. Repeat/stop and a host pass-through command still
need bindings. The physical keyboard and combinations must be checked with the
user, including how a simultaneous chord is reported by the keyboard hardware.

## Flash and SD

The detected board was previously identified as 8 MB flash without PSRAM. A
single factory application partition can use most, but not all, of this space:
the bootloader, partition table and persistent configuration need reserved
areas, and app alignment and actual flash size must be verified. An optional
filesystem or OTA partition costs space. Do not call an untested partition map
safe, and do not flash a changed layout until the built image, embedded table,
device identification and recovery method are checked. A one-slot design
maximizes firmware capacity but cannot provide normal two-slot OTA rollback.

SD is appropriate for recordings, music and other user files, speech resources
if the selected engine supports loading them, and other non-executable assets.
ESP32 native code cannot simply execute from a normal FAT SD file; moving
application code there requires an interpreter or another deliberately designed
and tested loading mechanism. The first implementation should link app code
into the single image and measure flash, heap and largest allocation. Do not
delete or reformat existing SD content. Missing or removed SD must be reported
accessibly and must not prevent the home screen and reader from starting.

## Acceptance before replacing the diagnostic

Build the image and verify its actual partition boundaries and flash usage.
Run meaningful host tests of dispatch, settings, app lifecycle, file error paths
and audio coordination. On the actual ADV, test speech, shortcuts, every app,
recording playback, Bluetooth pairing and release behavior, SD faults, battery,
time and Wi-Fi settings. Measure responsiveness and heap with Wi-Fi off,
connected and actively transferring, including playback and recording workloads.
The prior combined SD WAV/speech/Wi-Fi diagnostic is encouraging but does not
validate these applications or the new firmware. Installation and default boot
must be verified on the actual device after the firmware is ready; the user has
authorized replacing the factory firmware, which was already overwritten by a
diagnostic, but that does not make an incomplete new image usable.
