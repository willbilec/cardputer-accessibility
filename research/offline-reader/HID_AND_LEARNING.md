# Accessible keyboard/mouse and keyboard learning

The actual key learner now runs in [Velxio](VELXIO_RESULTS.md), but its live speech was judged extremely slow and crackly. Prerecorded speech through the same driver was clear. This is a material unresolved usability issue; host-test passes and compilation do not establish a dependable screen reader. The diagnostic buffering variant was not enabled in the SD apps.

Experimental headless **Cardputer ADV** programs for evaluation before purchase. No device, physical SD card, Bluetooth peer or USB host was connected. Software builds and host tests do not establish physical pairing, audio, latency, installation or keyboard usability.

## SD package and launcher

The package's `sd-card/accessible` directory contains:

- `bluetooth-keyboard-mouse.bin`: speech-enabled adaptation of [geo-tp/Bluetooth-Keyboard-Mouse-Emulator](https://github.com/geo-tp/Bluetooth-Keyboard-Mouse-Emulator), pinned at `2c1ca7a2fcf820d92aa0a0f238ce0d53a3a9995c`.
- `key-learning.bin`: new keyboard explorer, with no HID transmission.
- `media-player.bin`: existing headless speaking player, rebuilt with the shared keyboard support.

The speaking launcher lists all three programs and Help. Semicolon/period move, Enter selects, R repeats, Space stops speech. An installed matching program runs immediately; otherwise installation from SD requires another Enter. Only the current application slot is replaced. All three images remain on SD; one occupies internal flash at a time. These are not native programs executed directly from SD.

Each app's exact length/hash is compiled into the launcher. Missing, changed or oversized images are refused before erase. ESP image validation and a written-byte hash follow installation. Failure can destroy the previous application; the launcher is retained for retry. The existing fixed layout is unchanged. Physical installation/recovery remain unverified; the initial merged image is not a safe automatic upgrade to arbitrary device data.

## Keyboard/mouse controls

The upstream transport sources are adapted by a guarded build script; the downloaded checkout is unchanged. Its USB/Bluetooth selection and mode/connection indicators are replaced by spoken interaction. Both keyboard and mouse functions remain. Graphics dependencies are excluded and Klatt speech is linked into the firmware.

At startup choose USB, Bluetooth, Instructions or Return to launcher with semicolon/period and Enter. Bluetooth advertises as **M5-Keyboard-Mouse**. USB uses the S3 USB-OTG HID stack. Initial input mode is Keyboard. Go switches Keyboard/Mouse and announces the new mode.

Local reader commands use **Function + Option**, plus M for menu, H for help, R for repeat, Space to stop speech, Q to request the launcher. The menu offers Resume, Switch keyboard/mouse, Choose connection again, Instructions and Return to launcher. A connection change requests a confirmed restart. Return also requires Enter confirmation; key releases alone do not cancel it.

Held HID reports are released on mode/menu/exit transitions, and local command keys are suppressed until released. Connection/disconnection and initialization failures speak. Ordinary typing stays **silent** because the program cannot tell whether the remote computer is requesting a password. Host keyboard layout determines resulting characters.

Mouse: semicolon up, period down, comma left, slash right; Enter left button, backslash right button. Both buttons may be held. Button changes and newly pressed directions speak; continuous motion does not repeat announcements. This does not read the remote computer's screen.

Transport fixes include matching the Bluetooth four-byte mouse report to its descriptor, independent USB button release while other keys remain held, shared keyboard duplicate/rollover handling, and callbacks compatible with the installed NimBLE-based Arduino core. Tests of report generation are not proof of host acceptance.

## Keyboard learning

Press a key to hear its name and usual use. The learner recognizes letters/symbols, Shift/Control/Alt/Function/Option, Enter, Tab, Backspace, Space and Function-layer Escape/Delete/F1–F12/arrows. Letter labels use spoken names such as “letter bee.” Unassigned Function combinations are identified; application-dependent shortcuts are not assigned universal meanings.

The learner never starts Bluetooth or sends USB keyboard/mouse reports. Semicolon, period and Enter are described in exploration rather than executed. Go opens a spoken menu: Resume learning, Repeat last key, Instructions, Return to launcher. Semicolon/period navigate that menu; Enter activates. Function + Option reader commands work as above. Exit requires confirmation. Rapid presses interrupt older descriptions; the latest key/chord is retained for repeat.

Descriptions refer to this firmware and the Cardputer layout. Another application's use of a key can differ. This is a keyboard explorer, not a scored typing tutor or a remote screen reader.

## Build and evidence

```powershell
python run_host_tests.py
python build_package.py --headless
python emulation/verify_headless.py
```

The build produces all three apps before the catalogue launcher, using the existing 8 MB/no-PSRAM layout. `programs.py` supplies catalogue entries; generated hashes and staged files must match. The guarded transport adapter checks all four upstream file hashes. The manifest records exact artifact sizes and hashes. The package is staged locally; it has not been copied to a physical SD card.

All fourteen host suites pass; [advanced testing](ADVANCED_TESTS.md) adds installer fault injection, 30,000 randomized HID-loop transitions and sanitizer-backed mixer checks. They cover key layers/descriptions, modifiers and rollover, real menu controllers, descriptor sizes, catalogue selection and shared board/installer/speech behavior. Tests also execute the actual HID and learner event loops: initialization failure/recovery, silent typing, key and mouse-button release while other keys remain held, local command isolation, exploration without Enter activation, repeat and release-safe exit confirmation. Board and transport fakes deliberately do not simulate electrical devices, radio or the receiving computer. The prior linked-symbol audits passed for all four packaged images and confirmed no HID transport in the learner.

| Firmware | App image bytes | Linker static RAM bytes |
| --- | ---: | ---: |
| Bluetooth/USB keyboard and mouse | 992,608 | 104,760 |
| Key learning | 630,544 | 66,800 |
| Media player | 1,600,160 | 91,788 |
| Catalogue launcher | 696,032 | 67,016 |

Static RAM excludes runtime allocation. The prior successful Klatt/music/Wi-Fi emulator workload does not establish Bluetooth HID, USB enumeration or these complete programs' runtime memory. Hardware pairing, keyboard/audio coexistence, long-term stability, battery behavior and firmware switching remain unresolved.
