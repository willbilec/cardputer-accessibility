# Experimental Cardputer ADV speech test

These builds are unverified prototypes. No Cardputer ADV was attached during development. Do not flash over an existing installation: its partition table and compatibility with Launcher installation have not been verified.

The `cardputer-adv-calculator` and `cardputer-adv-timer` targets start in different test apps. Both also include a simple demo menu, reached with Escape or the backtick key, and share the same eSpeak/M5 audio and Wi-Fi code. They are separate firmware builds, not dynamically loaded apps.

The demo menu offers `l` to request a return to the paired experimental launcher. It checks for the expected `app0` test partition and reads the launcher boot settings without modifying them. For the inspected `launcher_keyboot` bootloader, a brief deep-sleep wake selects the launcher when deep-sleep return is enabled and no override GPIO is configured. If those settings prevent that path, the app reports that return is disabled. This depends on the actual installed bootloader and has not been tested on a device; the partition check alone cannot prove the bootloader is compatible.

The timer adapts the numeric input and seconds/minutes/hours controls from [foopod's MicroHydra timer](https://github.com/echo-lalia/MicroHydra-Apps/tree/main/app-source/timer). Enter starts the timer, `r` announces remaining time, and any key dismisses its repeating spoken alarm. In either app, `p` repeats the most recent announcement and `x` stops speech. This C++ adaptation has not yet been compared interactively with the original app or tested on a Cardputer. The calculator currently supports one binary arithmetic operation and is a diagnostic app, not a full port of the MicroHydra calculator.

For C++ test apps, `src/speech_service.h` exposes initialization, asynchronous speech, stop, repeat, and a bounded wait for special cases such as returning to the launcher. Call these from the UI task after initializing M5Cardputer; requests copy a bounded amount of text and replace earlier speech. A successful enqueue does not prove that audio was produced. The service currently uses a fixed English voice and rate, and is an experimental API rather than a general accessibility tree or a MicroPython binding.

From the demo menu, `w` attempts to connect using credentials already stored in ESP32 NVS, `s` starts an asynchronous scan, and `o` turns Wi-Fi off. USB serial at 115200 baud also accepts these newline-terminated commands, with literal tabs between fields:

- `WIFI<TAB>SSID<TAB>password` connects for the current run without deliberately persisting the supplied credentials. The command is not echoed or printed by the firmware; the terminal program may have its own local echo or history.
- `LOAD<TAB>host<TAB>port` sends test bytes to a TCP receiver for up to 30 seconds and speaks a recurring status phrase. `STOP` requests early termination. For a local receiver, run `python tcp_sink.py --port 9876` on the computer and use its LAN address as `host`.

The firmware logs transferred bytes, free/minimum heap, synthesis queue time, and time to the first submitted PCM buffer. These logs are instrumentation, not measurements until a device is run. Actual audio latency includes additional speaker buffering. Wi-Fi connection, load testing, and command handling are unverified on hardware.

Build using `python -m platformio run -e cardputer-adv-calculator -e cardputer-adv-timer -j 6` from this directory. The build depends on the sibling research checkouts referenced in `platformio.ini` and the toolchain in `../Launcher/.pio/core`. Each target creates an app-only `firmware.bin` and a separately merged `firmware.factory.bin`; neither has been verified for installation, booting, or recovery through Launcher.

The portable app logic can be tested without a device by compiling and running `test_models.cpp` with a C++17 compiler. `test/test_network.cpp`, compiled with `test/fakes` on the include path, exercises the actual serial command handler with simulated Wi-Fi, task, and socket objects, including credential redaction, duration, cancellation, invalid inputs, and oversized commands. `test/test_launcher_return.cpp` exercises partition and NVS outcomes plus the sleep request with fakes. `test/test_speech_service.cpp` tests the real queue code against fake synthesis and audio, including replacement, repeat, cancellation and initialization failure. These tests do not test the keyboard, actual synthesizer, codec, audio output, real Wi-Fi, app installation, actual reboot, or screen-reader usability.

To check the upstream MicroHydra calculator and timer logic with simulated
devices, run these commands from the repository root.

```sh
python fetch_dependencies.py MicroHydra-Apps
python research/test_calculator_host.py
python research/test_community_timer_host.py
```
