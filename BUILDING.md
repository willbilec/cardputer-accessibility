# Building from source

## Firmware Builder for Windows and Linux

On 64-bit Windows, extract the entire release ZIP to a writable folder and
open **Cardputer ADV Firmware Builder.exe**. The ZIP includes Python and the
builder dependencies. The GitHub release ZIP opens the GitHub catalog
automatically. The default app checkboxes match the precompiled
universal firmware with the LCD and help-text options off. The builder
checks the archive and saves a flashable build. Other selections use Git
and the pinned compiler, source and ESP32-S3 SDK. Use the checkout or source
archive when testing changes to local help and setup resources.

To regenerate the Windows ZIP, run
`python builder/package_windows.py release-files/Cardputer-ADV-Builder-Windows-x64`
from a 64-bit Windows virtual environment with the builder requirements installed.
Add `--with-source` to package production firmware sources with a local preview.
The packaging command keeps the finished ZIP and removes its staging directory.

Use Python 3.12 or later and install Git. Create a Python virtual environment and
install the builder dependencies. On Linux, extract the source archive from
the release or clone the repository. wxPython also needs GTK. Use your
distribution's wxPython package with a system-site-packages virtual environment
if a wxPython wheel is unavailable for your distribution. In the builder,
choose **GitHub release** to use the published firmware and **Local source**
to build the checkout.

```sh
python -m pip install -r builder/requirements.txt
python builder/bootstrap.py
```

Use Tab to reach the app list, the arrow keys to move, and Space to toggle
the focused app. Each row shows the name, inclusion status, description and
version. Voice Recorder and the Bluetooth/USB keyboard can each be omitted.
Use **Refresh USB ports** or F5 after connecting a Cardputer. Select an image
in **Saved firmware builds** and choose **Flash selected build** to reinstall
it. **Tools → Browse for a saved build** accepts a `build.json` from another
folder. The Tools menu also contains catalog refresh, builder and firmware
updates, partition installation and SD updates. The default selection
includes every native app except Keyboard learning. That module can be
selected under **Help options** and appears in the firmware Help menu.
The group can also package help text for
selected apps. Clear the app check boxes and Help options to omit both.
Check **Include LCD display** to build the visual interface and backlight
settings. The check box is off by default. Screen-off builds omit the display
driver, menus, icons, and fonts. For command-line builds, pass `--screen`.
For custom builds with the Bluetooth/USB keyboard selected, its source downloads in
parallel with the audio sources. PlatformIO
then installs its toolchain and any libraries for the selected apps. When help
text is selected under **Help options**, the builder downloads the common help text and documentation
for selected apps only and writes it to `sdcard/Help` in the output folder.
After flashing and verifying a compatible launcher, the builder copies
and verifies the text over USB with the microSD card still in the Cardputer.
The text stays in the build folder if the card or transfer is unavailable.
With this option on, Bible Reader
also downloads its setup guide and Old Testament sorting scripts into
`workspace/help/current`.
The status bar shows the current download percentage. The activity log keeps
completed transfers and build phases. The builder
chooses a job count from available logical CPUs and free RAM. The program's
`workspace` folder holds downloaded sources, packages, the toolchain, and
finished firmware builds. Repeating the same release selection reuses the
verified saved image. Custom builds keep the current compiler objects and
a bounded cache. The desktop builder keeps the current and one previous
automatic recovery bundle, and clears stale release caches and temporary
toolchain downloads. Save any firmware you want to keep long-term to a
separate folder. Keep the workspace when updating the program.

On Windows, close the builder before running
`powershell -File builder/clean_workspace.ps1` from the checkout to see
which stale caches and generated bundles can be removed. Add `-Apply` to
perform the cleanup. The script keeps the two newest workspace bundles and
leaves device backups alone.

The builder checks for program updates on GitHub. It verifies the SHA-256
checksum of each updated Python file and loads the new program the next time
you open the builder. Use **Tools → Check for builder updates** to check
manually. To move from Windows builder v0.2.8 or earlier to the compact release
format, download the newer Windows ZIP once. Keep the existing `workspace`
folder to carry forward saved firmware and caches. A runtime compatibility
change also requires the Windows ZIP or updated Linux dependencies.
When a single Cardputer is connected at startup, the builder also checks
its reported firmware and app versions against GitHub and summarizes
available updates and newly published apps.

**Refresh GitHub catalog** switches to the latest published catalog and keeps
the current app selections. **Update installed firmware** reads the current
launcher and app versions over USB and offers to preserve the selection.
**Update SD packages** works on a mounted card. It stores versioned data under
`CardputerPackages`, so a failed copy leaves the previous version available.
Use the Cardputer file browser to open the installed guide.

For an app-only update the device must already have the supported partition
table. When a flash check detects a different layout, the builder offers
to use the finished build to install the large partition before writing.
**Tools → Install large partition** can replace an existing layout with one
7.875 MiB application partition. The builder offers a full flash backup as an
option because some devices reset during long reads. If a requested backup
fails, the builder stops before writing. The saved build can reinstall the
new firmware. Restoring the previous firmware and settings requires their
own recovery image or backup. See [Flashing and recovery](FLASHING.md).

Before tagging a release, use NVDA on Windows and Orca on Linux to check
module selection, progress, errors, and the recovery confirmation with the
keyboard. On a spare or recoverable ADV, check a full build and a reduced
build. Listen for the startup announcement and check the Home apps with
Wi-Fi on and off. Check a normal app-only update, an interrupted SD package
upgrade, and USB recovery with the saved build. Use a device with an older
layout to validate the one-time migration separately.

### Adding modules and releasing

#### Register a native app

Put implementation files in `research/offline-reader/src/` and headers in
`research/offline-reader/include/`. Add an object under `modules` in
[`builder/modules.json`](builder/modules.json). Use a stable lowercase ID
with hyphens, a short name and description for the builder, and a three-part
version. Set `introduced` to the launcher version that first offers the app
so the builder can identify it as new. Keep that value on later updates.

```json
{
  "id": "counter",
  "name": "Counter",
  "description": "Count up or down and hear the current value.",
  "version": "0.1.0",
  "introduced": "0.3.0",
  "kind": "native",
  "sources": ["counter.cpp"],
  "entry": "counterApp",
  "help": "counter.txt"
}
```

List source basenames in `sources`. Add `flags`, pinned `libraries`,
`scripts`, or shared component IDs in `requires` when the app needs them.
For a Home app, define `entry` at global scope with external linkage, for
example `extern const launcher::App counterApp = {open, key, tick, close};`.
The builder declares the symbol and generates its Home registration. See the
[launcher interface](research/offline-reader/include/launcher_app.h) for
callback signatures. Supply open and key callbacks. Pass `nullptr` for
unused tick or close callbacks. Return false from open after cleaning up a
failed start. Global shortcuts run before app key-down events. Use the shared
speech and Home services and follow the [accessible app guide](ACCESSIBLE_APP_GUIDE.md).
Features that extend an existing screen can omit `entry`.

For data installed separately to the card, use `kind: "sd"`. Set `source`
to the repository-relative data file and `filename` to its basename on the
card. See the `controls-guide` entry for an example.

#### Write on-device help and setup instructions

- Put a UTF-8 plain text file such as `counter.txt` in [`builder/help/`](builder/help/)
  and set `help` to that basename. Use letters, digits, hyphens or underscores
  before `.txt`. Start with the app's title, then explain how to open it, the
  main keys, required files or connections, and how to exit. Spell out key
  names and use short lines that read naturally aloud.
- Keep **Option + H** in the shared shortcut handler. Announce essential
  keys when a registered app opens. Update
  [unified controls](research/offline-reader/UNIFIED_CONTROLS.md) when keys or
  workflows change. The on-card text is an additional reference.
- For apps with setup steps, set `requires_setup: true`. Put the guide and
  scripts under `builder/help/`. Set `setup_guide` to its repository-relative
  path and `setup_assets` to the script basenames. The Bible reader entry
  shows this arrangement.
- With the app and **Include help text and download setup files for selected
  apps** selected, the release builder downloads the checksummed files from
  the tagged source. It packages the on-device text into
  `<build>/sdcard/Help/` and copies it to `/Help/` after a verified flash.
  Setup guides and scripts are for the computer and stay in
  `workspace/help/current/`. Keep these files with the source outside the
  Windows runtime and program update file list.

#### Verify and publish

Build the app by ID and inspect `build.json` and `sdcard/Help/`.

```sh
python builder/build.py workspace/builds/counter counter --include-help
```

Build again with the app omitted to check that its Home
entry and app-specific dependencies are absent. Also test it with the
standard app selection. Use the keyboard and speech checks above and record
which device checks still need a physical run.

Increase the version of a changed app or SD package when preparing a
release. Set the launcher version in `builder/modules.json` and tag that
commit `v<launcher-version>`. The
[release workflow](.github/workflows/builder.yml) runs the builder tests,
packages the Windows builder, and builds one universal image with
`--preset standard`. GitHub provides the source ZIP and TAR.GZ. The builder
reads catalog and updater metadata from a hidden block in the release notes
and fetches individual help, setup, and SD files from the tagged source.
Preserve that block when editing published release notes.

### Shared firmware components

Put reusable build inputs in the `components` section of
`builder/modules.json`. A component may list `sources`, `flags`,
`libraries`, `scripts` and its own `requires`. An app lists the component
IDs it needs in `requires`. The builder resolves that chain, bundles each
required component once, and omits components with no selected consumer.
It records the resolved IDs in the saved `build.json`. Unknown IDs and
dependency cycles fail before compilation.

PlayPalace requires `vorbis-codec` for Ogg effects. Bible Reader requires
`mp3-codec`. The optional MP3 and Ogg file playback module requests both for
the file browser. Both codecs use the same `media-decoder` component. The
compressed file playback pipeline is shared by the Bible reader and file
browser, while PlayPalace uses the decoder through its effects mixer.
The base launcher retains WAV playback and offline speech. When adding a shared
component, keep its optional code behind its build flag and verify a build
with the consumers selected and a build with all of them deselected.

## Firmware-only command line

Install Git and Python, then clone and build:

```sh
git clone https://github.com/willbilec/cardputer-accessibility.git
cd cardputer-accessibility
python -m pip install platformio==6.2.0
python builder/build.py workspace/builds/full --preset all
```

Choose a new output folder for each build. The output includes the four
matching flash binaries and `build.json` with versions and checksums.
Use `--preset core` for the base launcher, `--preset standard` for the
published universal selection, or specify individual app IDs,
for example `python builder/build.py workspace/builds/online playpalace nvda-remote`.
Add `--keyboard-learning` to include Keyboard learning in Help and `--include-help`
to package help text for selected apps into `sdcard/Help`:

```sh
python builder/build.py workspace/builds/accessible bible --keyboard-learning --include-help
```
Direct PlatformIO targets describe historical configurations.

The preparation scripts adapt pinned copies of BackgroundAudio and the geo-tp HID project during the build. The [dependency manifest](research/dependencies.json) lists the upstream URLs and revisions.

## Host tests

Run `python -m unittest discover -s builder` from the checkout for builder
tests. For firmware host tests, fetch the test dependencies with
`python fetch_dependencies.py BackgroundAudio Bluetooth-Keyboard-Mouse-Emulator MP3PlayerForM5Cardputer`,
then run `python run_host_tests.py` from `research/offline-reader`.
The C++ tests need GCC with AddressSanitizer and UndefinedBehaviorSanitizer.
The Windows test runner invokes GCC through WSL. Results are saved under
`research/offline-reader/test-results`.
