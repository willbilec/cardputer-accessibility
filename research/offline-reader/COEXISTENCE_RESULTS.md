# Live speech, other audio and connected Wi-Fi

2026-11-04. **The user heard separate music and live English speech clearly together, at normal voice pitch, while Velxio reported a connected Wi-Fi station.** No physical device was used. This extends the earlier [speech/scanning trial](BACKGROUNDAUDIO_RESULTS.md): scanning alone was not a connected Wi-Fi test.

## Workload and result

`backgroundaudio-coexist` runs BackgroundAudio 1.4.4's real mixer at 44,100 Hz, live eSpeak-NG at 22,050 Hz and an independently decoded original arpeggio WAV from flash. The music is four seconds long and repeats. It is not a second prerecorded speech clip, and this is not an MP3 decoding or SD streaming benchmark. CPU frequency is 240 MHz, with no PSRAM, graphics or Bluetooth.

The compiled firmware joins the simulator's `Velxio-GUEST` network using `WiFi.begin`. It reports `WL_CONNECTED` (status 3), SSID and IP `10.13.37.42`. This verifies the application's connected state within the simulator. It does not establish a physical radio link, WPA2 authentication, battery performance or actual-device Wi-Fi/audio coexistence. The vendor describes [built-in networks and uploaded-firmware behavior](https://velxio.dev/docs/wifi-iot/esp32-wifi/); our prebuilt image names the network explicitly rather than relying on its compiler's SSID rewriting.

Final corrected image SHA-256: `a3c7324ddac2de736472c3a54da4b441a8fb17b32fc698b9982a209e92d1edd7`; application: 2,295,568 bytes. It fits the 2,621,440-byte diagnostic factory partition. The merged binary is simulator-only, not a hardware installation image.

- Mixer, speech and WAV initialization all reported success.
- The longer paragraph generated first non-silent speech PCM after 50 simulated milliseconds and reported library completion at 22,942 simulated milliseconds.
- Music remained enabled, the Wi-Fi status was 3 at speech completion, and music advanced through at least 15 repeated clips during the observed run.
- The physical-output driver reported zero underruns throughout that final run. This counter does not separately prove the absence of starvation on every mixer input.
- Free heap after speech was 125,920 bytes, minimum observed 111,504 bytes, largest block 81,908 bytes. These are simulator readings for this exact workload, not the complete community app.
- The user confirmed: “Both clear, normal voice pitch.” Simulated PCM submission and library completion are not measured audible wall-clock or device latencies.

## Problems found and corrected

These were concrete library/configuration problems in the new mixed path, not evidence of insufficient hardware performance:

1. `BackgroundAudioMixerInput::availableForWrite` promised bytes but returned stereo words. The speech producer waited indefinitely at the reduced buffer count. The generated dependency now returns bytes.
2. A single software input buffer could stall with startup silence occupying only part of a frame. The speech leg now uses **two software buffers**. The direct-output minimal test still requests one; these are different buffering paths. The physical mixer output uses the upstream four DMA buffers.
3. Input `begin()` overwrote the previously configured source sample rate with the output rate. Speech therefore played too fast/high at 44,100-Hz output. The user noticed the high pitch. Preserving the configured input rate corrected this, confirmed by the user and an actual-header host test that checks 22,050-to-44,100 sample duplication.

All dependency changes are guarded patches to `generated/backgroundaudio-trial`. The upstream checkout remains clean and pinned. The mixer capacity, partial/full writes and rate preservation pass AddressSanitizer/UndefinedBehaviorSanitizer host tests. All 16 host suites pass. These fakes do not emulate I²S, codec, Wi-Fi or real scheduling.

## Internet traffic remains unverified

A separate worker attempted repeated HTTP connections to `example.com` while music, speech and Wi-Fi association were active in an earlier mixer image. At least 11 attempts failed to connect; no response bytes were received. Audio continued, but **this is not a successful internet-traffic stress test**. No root cause for the outbound failure was established. The final pitch-corrected listening run had no HTTP load enabled. Connection/DHCP evidence should not be presented as proof of internet routing.

## Reproduction

Run `python emulation/build_background_trial.py --coexist`, then upload `test-results/backgroundaudio-coexist/backgroundaudio-simulator.bin` through Velxio's File → Upload firmware menu. `cm4` in Serial starts Wi-Fi association, repeating music and the long announcement. `n` toggles the HTTP worker; `m` toggles music; `s` stops speech only; `x` disables networking/Wi-Fi. The initial failed mixer versions and the native-rate comparison are diagnostic history, not the final result.

The next test is the [accessible CardEX adaptation](CARDEX_ACCESSIBILITY.md), which adds real application state and keyboard interaction. Its resource use and usability must be assessed separately.
