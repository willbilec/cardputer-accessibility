# Current project status

The active project is one unified Cardputer ADV firmware with shared offline
eSpeak NG speech and selectable native apps. The desktop builder can compile
app selections, flash over USB, and manage SD data packages. Its build cache
and generated recovery files stay in the program workspace.

- [Build and package the desktop builder](BUILDING.md)
- [Install, update, and recover firmware](FLASHING.md)
- [Use the keyboard and built-in speech](research/offline-reader/UNIFIED_CONTROLS.md)
- [Review physical test evidence](research/offline-reader/PHYSICAL_RESULTS.md)

The current firmware target uses a single 7.875 MiB application partition on
8 MiB flash. The Windows builder has been exercised locally, including USB
download-mode detection, partition verification before writing, and fetching
the published universal firmware. Tagged releases publish the Windows builder
and one universal firmware ZIP. GitHub supplies the source archives, and the
builder uses release-note metadata to find source and program updates. Live
screen-reader testing and physical acceptance of the latest builder-driven
flash and module combinations are outstanding.
