# Building from source

## Firmware Builder for Windows and Linux

On 64-bit Windows, extract the entire release ZIP to a writable folder and
open **Cardputer ADV Firmware Builder.exe**. The ZIP includes Python and the
builder dependencies. Install Git for firmware builds. The first build
downloads its pinned source dependencies and toolchain. The GitHub release ZIP
opens the GitHub catalog automatically. It downloads the build sources when
you first build. To package a local preview, add `--with-source`.

To regenerate the Windows ZIP, run
`python builder/package_windows.py release-files/Cardputer-ADV-Builder-Windows-x64`
from a 64-bit Windows virtual environment with the builder requirements installed.

Use Python 3.12 or later and install Git. Create a Python virtual environment and
install the builder dependencies. On Linux, wxPython also needs GTK. Use your
distribution's wxPython package with a system-site-packages virtual environment
if a wxPython wheel is unavailable for your distribution.

```sh
python -m pip install -r builder/requirements.txt
python builder/bootstrap.py
```

Use Tab to reach the app list, the arrow keys to move, and Space to toggle
the focused app. Each row shows the name, inclusion status, description and
version. Voice Recorder and the Bluetooth/USB keyboard can each be omitted.
The Tools menu contains catalog refresh, USB refresh, builder and firmware
updates, partition installation, saved-build recovery and SD updates. The
app list selects all available apps by default. Keyboard learning is an
optional item in the expandable **Help options** group; it appears in the
firmware Help menu when selected. The group can also package help text for
selected apps. Clear the app check boxes and Help options to omit both.
The first build downloads pinned audio sources.
Check **Include LCD display** to build the visual interface and backlight
settings. The check box is off by default. Screen-off builds omit the display
driver, menus, icons, and fonts. For command-line builds, pass `--screen`.
When the Bluetooth/USB keyboard module is selected, its source downloads in
parallel with the audio sources. PlatformIO
then installs its toolchain and any libraries for the selected apps. When help
text is selected, the build writes it to `sdcard/Help` in the output folder and
offers to copy it to a mounted microSD card. The on-device Help menu lists those
`.txt` files. `builder/help` contains detailed Bible setup instructions and Old
Testament sorting scripts (`bible-setup.html`, `sort_nlt_bible.ps1`, and
`sort_nlt_bible.sh`).
The status bar shows the current download percentage. The activity log keeps
completed transfers and build phases. The builder
chooses a job count from available logical CPUs and free RAM. The program's
`workspace` folder holds downloaded sources, packages, the toolchain, and
finished firmware builds. Compatible selections reuse compiler objects
while saving each completed image separately. Keep the workspace when
updating the program.

The builder silently checks for program updates from GitHub. It downloads the
individual application and help files, verifies every SHA-256 checksum, then
switches to the update only after all files pass. Updates take effect the next
time you open the builder. A runtime compatibility change requires the Windows
ZIP or updated Linux dependencies. You can check manually from the Tools menu.
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

To submit an app, add its source and an entry in `builder/modules.json` in a
pull request. A native entry lists its source files, flags, libraries and
version. For a new Home app, provide an `entry` symbol declared as
`extern const launcher::App` and defined with external linkage. The
[launcher interface](research/offline-reader/include/launcher_app.h) provides
entry, key, tick and close callbacks. Use its shared speech and Home services
and document the keyboard operation as described in the
[accessible app guide](ACCESSIBLE_APP_GUIDE.md). Global shortcuts are dispatched
before app key-down events. Supply both open and key callbacks. Return false
from open to return to Home after cleanup. Tick and close are optional.

Add a short `builder/help/<name>.txt` file and set the module's `help` field to
make it available for optional on-card help. For apps that need user-provided
files or another setup step, set `requires_setup: true` and `setup_guide` to a
guide path such as `builder/help/bible-setup.html`; the builder labels the app
as requiring extra setup and shows that guide path.

An SD entry lists a source data file and its name on the card. Increase the
changed component's version when preparing a release. A release pins a
compatible launcher and collection of apps together. After review, a
`v<launcher-version>` tag runs the build matrix and publishes the checksummed
source archive, SD packages and catalog as GitHub Release assets.

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
Use `--preset core` for the base launcher or specify individual app IDs,
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
