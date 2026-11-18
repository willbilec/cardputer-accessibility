# Building from source

Install Git and Python, then clone and build:

```sh
git clone https://github.com/willbilec/cardputer-accessibility.git
cd cardputer-accessibility
python -m pip install platformio==6.1.19
python fetch_dependencies.py
cd research/offline-reader
python prepare_background.py
python -m platformio run -e unified-playpalace -j 6
```

PlatformIO downloads the pinned ESP32 platform and builds the firmware in `.pio/build/unified-playpalace/`. Its `firmware.bin`, `bootloader.bin`, and `partitions.bin` are used for installation. The fourth file, `boot_app0.bin`, comes from `packages/framework-arduinoespressif32/tools/partitions/` inside the PlatformIO core directory. Run `python -m platformio system info` to locate that directory. Keep all four files from the same toolchain and build.

`unified-playpalace` is the release target with the 8 MiB layout. Earlier `unified` and `unified-dev` targets use the original reader/extra-app partition layout. Their images need the matching partition table.

The preparation scripts adapt a pinned copy of BackgroundAudio and the geo-tp HID project during the build. The [dependency manifest](research/dependencies.json) lists each upstream URL and revision. `python fetch_dependencies.py --all` fetches the additional historical dependencies. [Historical experiment notes](research/patches/README.md) explain how to apply the separate app adaptations.

## Host tests

From `research/offline-reader`, run `python run_host_tests.py`. The C++ tests need GCC with AddressSanitizer and UndefinedBehaviorSanitizer. On Windows the runner invokes GCC through WSL. Python integration checks verify the generated adapters against their pinned sources. Results are saved under `test-results/`.
