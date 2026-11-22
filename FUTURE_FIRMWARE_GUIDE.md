# Designing future accessible Cardputer ADV firmware

Use the [app authoring guide](ACCESSIBLE_APP_GUIDE.md) for a first in-firmware
app and the [speech integration guide](TTS_FIRMWARE_GUIDE.md) for driver,
speech engine and mixer details. [Project status](PROJECT_STATUS.md) records the
implementation history and links to the underlying experiments.

## Architecture

- Keep one owner for keyboard dispatch and one owner for the ES8311/I²S
  audio path. Initialize shared services once. Mix speech and other audio
  explicitly, and check sample rates and buffer units at the boundaries.
- Give apps entry, input, update and exit responsibilities. Route global
  commands before app keys. Consume local shortcuts before sending HID
  reports. Release held reports and wait for physical key release at mode
  transitions. Give users a spoken or audible way to discover the current mode.
- Expose semantic focus, values, errors and results from app state. Bound
  speech requests and pending announcements. Replace stale focus, carry long
  help through to completion, and expose Stop, Repeat and Help. Preserve the
  independent speech rate/volume and media volume controls.
- Keep default sensitive input silent and avoid retaining secrets in speech
  context or diagnostics. The current unified Wi-Fi password screen speaks
  characters while redacting them in diagnostic output. Treat that as an
  existing behavior to address explicitly when changing this flow.
- Budget flash, minimum heap, largest free block and task stack headroom. The
  ADV has 8 MB flash and no PSRAM. Load decoders and radio resources on demand.
  The unified Bluetooth path turns off Wi-Fi and suspends speech to free RAM.
  It uses cues during Bluetooth operation and restores speech after exit.
  Test this mode boundary and give users accessible feedback if startup fails.
- Keep serial diagnostics off timing-sensitive paths. Make routine logging
  nonblocking, including when the PC has no active reader. A previous
  connected-but-unread USB serial path could stall for about two seconds.

## Network callbacks, speech and task placement

- Use the firmware's shared saved-network connection service for apps that
  need Wi-Fi. Calling `WiFi.reconnect()` alone does not load the credentials
  saved by the launcher. A successful host-side connection check also does
  not prove that the Cardputer has joined the same network or can reach the
  device; verify both ends separately and report the app's actual connection
  result accessibly.
- Keep long-running network clients off the navigation/speech loop. NVDA
  Remote and PlayPalace own their socket in a worker pinned to core 0 and
  exchange bounded events and outgoing commands with the app on core 1. Keep
  one owner for each socket; do not call `sendTXT()` from its receive callback
  or run a persistent `socket.loop()` beside speech on core 1. Process queued
  events and make their announcements from the app loop. If a library callback
  exposes a temporary payload, copy or acknowledge it before returning.
- Keep menus and commands unavailable until a valid response from the target
  confirms the connection. A socket-connected event or connected Wi-Fi alone
  does not prove that the printer has returned usable status.
- Keep the connecting screen separate from the screen to restore. Repeated
  disconnects can otherwise save the connecting screen as the destination,
  leaving the menu locked even after a valid status reply. Track pending
  requests separately from command numbers when zero is a valid command.
- Let the connection prompt finish before announcing readiness. Both events
  can occur within one spoken phrase, and interrupting the first phrase can
  sound like stutter. Keep keyboard navigation available as soon as the
  target confirms readiness.
- When a connection drops only on one command, capture a read-only reply and
  compare its size with the transport library's receive limit. A directory
  reply of about 25 KB exceeded the pinned WebSocket library's 15 KiB limit
  and caused a protocol close. Keep receive limits bounded and filter large
  JSON replies to the fields the app uses.
  A transport setting must reach the library compilation. The builder routes
  module `-D` flags to app sources, so a library setting needs its own build
  script or library configuration. Prefer a per-client override when only
  one endpoint needs the larger receive limit.
- When investigating audio glitches during a network session, compare Wi-Fi
  power save and optional eSpeak tempo processing with NVDA Remote as well as
  task placement. Restore session settings when leaving the app.
- Advance the polling timer on every attempt, including failures before a
  command is sent. Announce a recurring background error once until it
  clears, and keep explicit user actions audible. An unset clock prevented a
  printer status request from being sent and left the poll overdue. The app
  restarted the same eSpeak warning every loop, which sounded like static
  on a stuck word.
- A saved clock value survives shutdown as a snapshot. For protocols that
  accept approximate timestamps, use that snapshot plus boot uptime while
  network time synchronizes. Keep the estimate local to the protocol so
  certificate checks, time displays and other apps still use the system clock.
  A snapshot cannot account for time spent powered off. If there is no saved
  value, request shared network time and give a single actionable warning.
  PlayPalace can sign in with an unset clock because its sign-in request does
  not require a client timestamp. Check the target protocol before making
  clock sync a connection prerequisite.
- Do not pin work to another CPU core just because an app appears frozen.
  First identify the blocking call and measure loop latency, heap and audio
  behavior under the reproducing workload. In a printer control app, status
  speech stuttered while `WebSocketsClient::loop()` ran on core 1, and chained
  staged-file commands called `sendTXT()` inside the receive callback. Moving
  the socket owner to a core 0 worker and queueing events and commands follows
  the proven NVDA Remote/PlayPalace pattern and fixes both ownership hazards.
  Use that pattern for persistent socket work; keep small, infrequent calls on
  the app core only when they do not compete with speech.

### Cardputer printer follow-up

The user confirmed the core printer app worked on the Cardputer with version
0.1.8. The host also received a valid reply to a read-only status request
dated one day in the past. That probe establishes tolerance for that status
request, and the on-device report records the working app as a whole. A brief
speech stutter during clock acquisition had been reported before 0.1.8. Its
specific cause was not isolated.

## Installation and recovery

Distinguish app-only binaries from merged simulator or installation images.
Read the target's actual partition table and verify the app offset, size and
alignment against the exact binary. The current builder targets the
7.875 MiB application partition at `0x10000`. The
[flashing guide](FLASHING.md) covers partition verification and migration.

Record the image size and SHA-256, bootloader, USB port and device identity.
Preserve existing SD data and flash settings. Before changing an installed
layout, verify a usable backup or recovery image and a route back through USB
download mode. The historical full-flash backup failed partway and cannot
serve as a complete recovery image. Validate installation, reboot and
recovery on the actual layout before claiming those operations work.
See [physical flash results](research/offline-reader/PHYSICAL_RESULTS.md#flashing-and-recovery).

## Verification

1. **Build.** Record the source revision or working-tree state, dependencies,
   environment, image hash, size and partition fit.
2. **Host.** Exercise semantic focus, commands, speech replacement, cleanup,
   cancellation and the applicable storage or network fault paths.
3. **Emulator.** Check keyboard navigation and listen to complete phrases,
   including long help. Note emulated peripheral limitations and browser
   focus behavior. The CardEX Edge test and the failed live Klatt test show
   why firmware execution and usable audio need separate evidence.
4. **Physical ADV.** With the user's normal keyboard and audio setup, test
   startup, every new screen, mode changes, Stop/Repeat/Help and errors. Run
   sufficiently long sessions with Wi-Fi off, scanning, connected and actively
   transferring, including applicable SD, playback and recording loads.
   Check headphones, battery and idle recovery separately. Test Bluetooth
   pairing, switching and exit on the supported hosts if the change affects HID.
5. **Record results.** Log the exact workload, free/minimum/largest heap,
   stack headroom, underruns and key/request-to-first-PCM timing as applicable.
   Record audible onset and glitches separately. Zero driver underruns can
   coexist with an audible pause. Update [project status](PROJECT_STATUS.md)
   with what passed and what remains open.
