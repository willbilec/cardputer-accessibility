# Offline speech for Cardputer ADV firmware

The unified firmware uses pinned BackgroundAudio and eSpeak NG sources.
Its launcher owns keyboard input, the DAC and I²S path, speech, and the
shared mixer. A native app uses the existing [launcher interface](research/offline-reader/include/launcher_app.h)
to announce focus and state. Keep one reader and mixer in each image.

For standalone firmware, integrate a reader into the application itself.
Give one service ownership of audio output and request arbitration. Provide
speech, interruption, Stop, Repeat, independent speech and media volumes,
and semantic announcements from application state. Split long text at
natural boundaries and keep the keyboard responsive while it is spoken.
Keep sensitive input out of the repeat buffer and diagnostic output.

Use the current [source build instructions](BUILDING.md) for the unified
firmware. The [app guide](ACCESSIBLE_APP_GUIDE.md) describes keyboard and
speech behavior. The [firmware design guide](FUTURE_FIRMWARE_GUIDE.md)
covers lifecycle and resource ownership.

Verify startup, speech replacement, long help, and mixed audio on the
physical ADV. Check Wi-Fi off and on, and listen on both output paths
where relevant. Report heap, timing, and underruns separately from
listening results. Keep build and host-test evidence separate from
physical acceptance. Use the [flashing guide](FLASHING.md) for the
verified partition layout and recovery procedure.
