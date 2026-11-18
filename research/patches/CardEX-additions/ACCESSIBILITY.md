# Experimental speaking simulator build

See the [controls, test evidence and limitations](../offline-reader/CARDEX_ACCESSIBILITY.md). This modifies the actual CardEX app at upstream base `87506624e3e2e0e4bc8e1cb2770b56e94372d185`.

Build with `python ../offline-reader/emulation/build_cardex_trial.py`. The merged image is for the simulator only; it is not a physical-device installation or recovery image. The normal upstream configuration remains in `platformio.ini`; the separate trial configuration is `platformio-access.ini`.

The emulator profile excludes LCD transfers, cosmetic RGB timing, image viewing and USB mass storage. A FIFO-polling reader handles emulated keypad events. Ctrl is an additional Fn modifier: semicolon/period navigate, Enter opens, Ctrl+H speaks help, Ctrl+Space repeats context, Ctrl+Shift+Space reads more and Ctrl+Backspace stops speech.

The polling-reader test compiles the actual class against fake controller transport, using the pinned M5Cardputer base interface: `g++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -Itest/access_fakes -I.pio-access/libdeps/cardex-access/M5Cardputer/src test/test_access_keyboard.cpp -o /tmp/cardex-access-keyboard`. Execute the resulting binary. It tests all 56 mappings and edge/bounds handling, not physical I²C or interrupt behavior.
