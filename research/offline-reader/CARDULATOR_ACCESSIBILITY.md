# Speaking Cardulator utility: simulator results

2026-11-04. A second community application, [Cardulator](https://github.com/aroum/cardulator), has been adapted with offline speech. This scientific calculator and scripting utility retains the original calculation and interpreter engines and adds semantic announcements and a spoken tools menu. See [archived controls and build notes](../patches/cardulator-additions/ACCESSIBILITY.md) and the [TTS firmware guide](../../TTS_FIRMWARE_GUIDE.md).

## Deliverable

In Edge's Velxio ADV example, choose **File → Upload firmware → Run** with:

```text
research\offline-reader\test-results\cardulator\cardulator-access-simulator.bin
```

Final SHA-256: `29e230bcef06f02fb675ef23beba4c2d4f0fd15eb9d695cd41b3a794542102df`.

Application image: **2,670,000 bytes**. Linker static RAM: **117,380 bytes**. The verified factory partition starts at `0x10000`, size `0x7e0000`; the merged image's partition-table bytes match the generated table. This is a simulator-only factory layout, not a physical installer or SD launcher package. It does not replace the older SD catalogue images.

Press **Ctrl+O** for tools, **semicolon/period** to select a tool, and **Enter** to open it. Ordinary calculator punctuation types math. **Ctrl+semicolon/period/comma/slash** provide navigation; **Ctrl+H** reads help; **Ctrl+Space** repeats; **Ctrl+Backspace** stops speech. The detailed control guide covers formula parameters, scripts and plot samples.

## Runtime evidence

These are browser-emulator checks of the compiled ESP32-S3 application, not host fakes. Agent-driven keys were delivered through the emulator's keypad. Serial commands used the upstream application's input/evaluation path; these are identified separately.

The final image initialized polling keyboard and speech successfully. Speech initialization reported 143,532 bytes free heap and a 98,292-byte largest block. The ready announcement followed SD/settings initialization. A later sample at 60,000 simulated milliseconds reported 125,360 bytes free, 110,884 minimum, and zero driver-reported underruns. Simulated clock values are not measured wall-clock latency.

Final-image checks:

- Serial `conv(1,"furlong","m")` spoke **201.168 m** through the compact GNU Units backend.
- Serial `conv(5,"mile","deg")` spoke **Incompatible units: mile and deg** without resetting. A real Enter key cleared the error state, and subsequent serial `2+3*4` spoke **14**.
- Actual keys opened the tools menu, selected Settings, changed brightness **128 → 143 → 128**, and announced each value. Headless changes store values without accessing the physical display.
- Actual keys selected Constants, speaking pi's value, and Keyboard bindings, speaking the selected `s`/`sin(` binding.
- Ctrl+H produced the mode's help; Ctrl+Backspace produced `READER_STOP`; the app still answered `STATE`. This confirms command handling, not audible Stop latency.

Earlier builds of the same adaptation additionally exercised:

- Actual calculator keys `2+2` → **4**, variable `temp` → **25**, entry to its value editor and return.
- The original `fib` script, producing ten output lines, with Ctrl+N reading the next line.
- The original `hypot` formula wizard with parameters **3**, **4** → **5**. Testing found and corrected a speech omission: the selected next parameter is now announced even when a previous result exists. This was verified in image `c5b747a387a0d1d781af299a37c5bb204fe88a861612a80eb165cc4b295c935f`; the final image additionally suppresses physical brightness/rotation calls.
- Serial `plot([1,4,9])`, followed by actual keyboard selection of Plot data and Ctrl+N, spoke original sampled points: X **-10**, Y **1**, then X **-9.8000000000000007**, Y **4**. This supplies numeric samples, not an automatic verbal description of graph shape.

## Problems found and acceptance limits

The first build flooded the console with `task not found` watchdog warnings under Arduino 3. The loop now resets the watchdog only when subscribed.

The full GNU Units fallback catalogue caused an observed **memory allocation error in readunits**, followed by a reset. A successful mile/km conversion had used a direct handler and was insufficient evidence. The dedicated speech profile now compiles a compact common-unit catalogue and ignores SD catalogue overrides; the original direct handlers remain. Unknown or incompatible units return errors. It does not overwrite user catalogues. The complete catalogue is not supported in this profile.

The user heard startup crackle and initially thought the app had stopped responding. A running boot subsequently accepted actual keys and serial evaluation; the initial perceived idle hang was not established as a firmware crash. A separate, reproducible conversion reset was found and fixed as described above.

Moving speech after blocking startup work did **not** fully resolve the audible issue: the user reported crackle for about **10–20 seconds**, followed by smoother speech. The final automatic announcement was shortened, but that final startup has not received a new listening acceptance. Zero driver underruns do not disprove audible browser glitches. **Startup smoothness remains unresolved.** The user has not yet independently operated this new utility in Edge. CardEX's positive independent acceptance does not transfer to Cardulator.

Heavy synchronous scripts/calculations may delay keys and service polling. No arbitrary-script responsiveness guarantee is made. Physical speaker/headphones, RAM under hardware conditions, SD behavior, Wi-Fi coexistence in this utility, battery and firmware switching remain untested. No device was connected or flashed.

## Host checks and provenance

`test/run_native_access.sh` checks the actual raw-key and math-pronunciation adapters with address/undefined-behavior sanitizers; those pass. Separate compact-catalogue tests pass for conversion, incompatible dimensions, unknown names and recovery. The original math/script/help suite reports **31 tests, 1 failure**. Its incompatible-unit assertion expects “Incompatible”; the unchanged full-catalogue native path reports **Reduce error: mile**, with catalogue parsing errors. That failure is retained and the runner returns failure overall. It is not an all-green test suite.

Cardulator base: `bebc53ba2a1f37edb79d2ae78790b5b8aefb291a`. BackgroundAudio base: `94641100866beae2cc97dc925c0d4562ad168879`. Generated speech and keypad copies are guarded against the accepted CardEX source hashes. Exact inputs and structured observations are archived with the experiment. [Inputs](../evidence/cardulator/inputs.json) · [Observations](../evidence/cardulator/observations.json)
