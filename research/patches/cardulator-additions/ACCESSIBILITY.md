# Experimental Cardulator with offline speech

This is a source adaptation of [aroum/cardulator](https://github.com/aroum/cardulator), based on commit `bebc53ba2a1f37edb79d2ae78790b5b8aefb291a`. Its scientific calculator, variables, constants, formulas, script interpreter and plot samples remain the original application. The added semantic adapter announces their state and provides desktop-accessible commands. GPL-3.0 licensing remains in LICENSE.

The `cardulator-access` profile is **a headless Cardputer ADV simulator experiment**. No physical device has been tested. It uses the newer Arduino framework required by the tested BackgroundAudio speech engine. It suppresses physical LCD transfers and the splash animation, using a one-pixel offscreen canvas to keep original rendering calls safe. The original profiles remain separate.

## Build

From the workspace root:

```powershell
python research/offline-reader/emulation/build_cardulator_trial.py
```

The script prepares guarded copies of the working CardEX speech and simulator keypad adapters, builds the real application, verifies that the application fits its partition, merges the image, and compares the embedded partition table with the generated table. Sources and image hashes are recorded in `../offline-reader/test-results/cardulator/inputs.json`.

Dependencies are sibling checkouts documented in the [firmware speech guide](../../TTS_FIRMWARE_GUIDE.md). Initialize Cardulator's `lib/tinyexpr-plusplus` and `lib/gnu-units` submodules at their recorded revisions. The separate test script uses sibling Unity v2.6.1 and Linux/WSL C/C++ compilers. Do not run simultaneous builds in this checkout.

## Load in Microsoft Edge

Open the [Velxio ADV example](https://velxio.dev/example/cardputer-adv-m5-buzzer), choose **File → Upload firmware**, select:

```text
research\offline-reader\test-results\cardulator\cardulator-access-simulator.bin
```

Then choose **Run**. Focus the emulated keyboard as for CardEX. Your desktop screen reader may need focus mode or its pass-next-key command. The firmware speaks through the browser's audio output. This merged simulator image has an experimental factory partition and is not an SD-launcher installation package.

## Keyboard commands

| Keys | Action |
| --- | --- |
| Ordinary characters, digits and punctuation | Enter a calculation or edit the current field |
| Enter | Evaluate, open or confirm |
| Ctrl+O | Open or close the spoken tools menu |
| `;` / `.` in the tools menu | Previous / next tool |
| Ctrl+`;` / Ctrl+`.` | Up / down in the original app |
| Ctrl+`,` / Ctrl+`/` | Left / right in the original app |
| Ctrl+backtick | Escape / back |
| Ctrl+H | Speak the current mode's help; Enter or Escape closes it |
| Ctrl+Space | Repeat current context |
| Ctrl+Shift+Space | Continue long context |
| Ctrl+Backspace | Stop speech |
| Ctrl+A, C, X, V, Z, Y | Original editor selection, clipboard, undo and redo behavior |
| Ctrl+N / Ctrl+P in plot mode | Next / previous sampled point |
| Ctrl+L in plot mode | Next sampled curve |
| Ctrl+N / Ctrl+P in script output | Next / previous output line |

Tools are Calculator, Variables, Constants, Formula wizards, Scripts, Settings, Plot data, and Keyboard bindings. Original physical Function-key shortcuts remain available. Use the tools menu on a desktop keyboard. Unlike CardEX, unmodified punctuation in the calculator types mathematical symbols. Type `+` with **Shift+=**, as on a normal keyboard.

Try `2+3*4`, then Enter. The result should be 14. Use Ctrl+O, period, Enter to inspect Variables. Use Ctrl+H for the actual commands in each mode. Mathematical operators and brackets receive spoken names; numerical plot samples provide a textual alternative to the graph. This is not a complete spatial description of the graph.

The original calculator clears an error on the next key. Enter clears the error without evaluating; another Enter retries the expression. In a formula wizard, Tab or Ctrl+period changes parameter; Enter evaluates all current parameter values and advances. Empty parameters evaluate as zero. Speech distinguishes the last evaluated result from the currently selected input.

## Memory and conversion limits

The complete GNU Units catalogue exhausted memory and reset the simulator when loaded alongside speech. A simple miles-to-kilometres success did not expose this because Cardulator's direct conversion handlers run first.

This profile therefore embeds the bounded vocabulary in `accessibility_units.dat` instead of the full catalogue. It includes common SI, distance, time, mass, volume, electrical and derived units. The original direct conversion handlers still run first. Other unit names produce errors. This profile intentionally ignores SD `units.dat` overrides so loading a large catalogue cannot silently restore that allocation failure; it does not overwrite those files. Temperature and special conversions continue to use the original direct handlers. The normal upstream profiles retain their original catalogue behavior.

Heavy synchronous calculations and scripts can delay key handling and speech-service polling. Browser emulation does not establish physical latency or responsiveness for arbitrary scripts. There is no runtime Wi-Fi integration added to this utility; the separate coexistence trial supplies only simulator evidence for speech/music and a connected Wi-Fi station.

## Verification

Run from this checkout under WSL/Linux:

```sh
bash test/run_native_access.sh
```

The added adapter test exercises actual raw-key edge/repeat handling and mathematical pronunciation with address/undefined-behavior sanitizers. Compact-catalogue tests check conversion, incompatible dimensions, unknown names and recovery. The upstream suite currently has **30 passing tests and one failing assertion** about the full-catalogue error wording (`test_units_conversion`): it expects “Incompatible”, while the original bridge reports “Reduce error: mile” after catalogue parsing errors. The test runner preserves that failure status rather than hiding it. These host tests do not emulate I²S, SD, keyboard hardware or physical timing.

See [runtime observations](../offline-reader/CARDULATOR_ACCESSIBILITY.md) for the exact simulator image and acceptance status.
