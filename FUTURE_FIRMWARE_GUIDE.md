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
