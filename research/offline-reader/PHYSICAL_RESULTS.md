# Cardputer ADV physical diagnostic results

2026-11-17. A physical Cardputer ADV (ESP32-S3, 8 MB flash, USB Serial/JTAG) was tested. These are short diagnostic trials, not acceptance of the launcher, player, community applications, battery life, headphones, SD access, or extended operation.

## Speech and scanning

The user heard the minimal BackgroundAudio/eSpeak diagnostic say “Keyboard ready” clearly. In a later run the user reported a long passage clear at normal speed while the physical device scanned and found four Wi-Fi networks. The output driver counted 14 underruns during that passage; first nonzero PCM was submitted 60 ms after the request. This timing excludes actual sound output and is not an acoustic latency measurement. The log labels these values `emulated_ms` even though this run was on hardware.

## Connected Wi-Fi, music and speech

The `backgroundaudio-coexist` diagnostic connected to a real Wi-Fi network, repeated an independently decoded built-in WAV and spoke a long passage at the same time. The user said both were clear at normal speed. The device repeatedly received HTTP 200 responses from example.com; in one logged interval 17 requests had returned 14,875 body bytes with zero reported failures. At speech completion the output driver reported zero underruns, 22,868 ms library completion time and a minimum free heap of 109,988 bytes. These measurements apply to this particular test, not sustained throughput or an MP3/SD player. Speech first PCM was 58 ms after the request, not measured audible onset.

The diagnostic accepts Wi-Fi credentials over USB without printing them and calls `WiFi.persistent(false)`; credentials need to be re-entered after a restart. The test does not intentionally save them to flash. No credentials belong in reports or logs.

## Keyboard menu

The subsequent diagnostic build adds a three-item spoken menu: Speech demonstration, Toggle music and Wi-Fi status. Semicolon moves backward, period forward, Enter selects and Space repeats the current item. The user confirmed keyboard navigation, music selection and Wi-Fi status. The status initially reported disconnected after flashing, then correctly reported connected after the computer re-supplied the credentials. The user confirmed the spoken result. The menu runs without a display, not as a complete launcher or accessibility tree.

Menu test image SHA-256: `b9fa00e0388cf13d39f8b5713677ea7af943cf22fe6adb4c6c7157686f392ce0`; app size 2,297,264 bytes. The build script names the merged image `backgroundaudio-simulator.bin` for historical reasons; this image was flashed as a diagnostic on the physical device, not validated as a general installation image. Source: `src/backgroundaudio_coexist.cpp`.

The user later reported intermittent pauses in both music and speech, particularly after idle. During one serial observation first submitted non-silent speech PCM took 1,579 ms; the output underrun counter stayed zero and periodic status continued. This does not identify the cause or establish acoustic latency. The four-second music clip repeats, and menu navigation wraps, which should not be confused with a reboot. The computer also issued commands and reflashed the device during parts of the session; these operations interrupt or may affect timing. An undisturbed, longer observation remains necessary.

The serial driver's connected-but-unread transmit path can block for approximately two seconds. The diagnostic printed from the main loop and audio callback, a plausible cause of the observed pauses. The follow-up makes normal serial writes nonblocking and moves first-PCM logging out of the audio callback. With the serial reader closed and the device idle for 30 seconds, the user reported that the menu was responsive again. This is a positive short test, not proof that all freezes are fixed. Diagnostic output can be dropped when the reader is absent; the explicit file-upload protocol temporarily restores a bounded transmit timeout.

## SD WAV and combined workload

The sample is a 19.174-second, 44.1 kHz, stereo, 16-bit PCM WAV from [SampleLib's test WAVs](https://samplelib.com/sample-wav.html), downloaded as `sample-15s.wav`. The source page describes it as a 19-second synth melody and permits unrestricted test use. It was copied to `/codex-audio-test.wav` on microSD; the existing-name check avoids overwriting another file. SHA-256 `33e2e7b2ffa021275a90a26704d923fe902d3600e4ffecf06253c57778a2a986` matched the source after the firmware read the entire file back. The first upload used oversized USB chunks and failed; a later acknowledged 128-byte transfer succeeded. Windows briefly reported a USB device descriptor failure, and the USB serial port returned after the user tried another cable/port.

The first SD playback stuttered. Increasing the feed from 1 KB every 5 ms to up to 4 KB every 1 ms eliminated the observed stutter in the next listening trial. The user reported smooth music; a separate timed run reported 19,189 ms from starting playback to decoder completion, close to the 19,174 ms file duration. These timings are software timestamps, not speaker capture. The output driver reported zero underruns; the input decoder counted two underflows over the timed run. Its `wav_frames` counter remained zero and must not be used as evidence of playback progress.

In the final combined trial, the user reported that the SD music and live speech were smooth together while the device was connected to Wi-Fi and repeatedly receiving HTTP 200 responses. The recording completed in 19,186 ms with two input underflows, zero output underruns and no reported WAV errors. Minimum free heap for the run was 59,920 bytes; this is a low-water mark for this diagnostic, not a guarantee for an application. The HTTP requests were small periodic responses, not sustained bandwidth saturation. The first attempted combined trial accidentally toggled music off and is not counted.

The currently installed diagnostic image SHA-256 is `76a9dfbecabf5c110a3b551e167b8e57bdeff12e4aa387e9c4c398534d08d5f7`; app size 2,364,384 bytes. Its SD music is a single file played once, not a general music library or MP3 player. Credentials are supplied anew after reboot. Headphones, arbitrary WAV formats, large-file seeking, SD removal, long sessions and battery operation remain untested.

## Flashing and recovery

After the first manual G0 download-mode session, the default reset left the device in download mode until power-cycled. Two subsequent flashes succeeded without manually pressing G0, using `--before usb-reset --after watchdog-reset` and verified hashes. See Espressif's [USB reset options](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-options.html) and [download-mode reset explanation](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/troubleshooting.html). Automatic entry depends on the running firmware leaving USB communication available. A watchdog reset has platform-specific limitations as documented by Espressif.

The initial attempt to back up all flash using esptool 5.3's stub repeatedly stopped around the same offset; one sector was read successfully using `--no-stub`. Espressif has a [similar USB Serial/JTAG read issue](https://github.com/espressif/esptool/issues/1155). The full backup was not completed; the user identified the existing firmware as factory firmware and chose to proceed. M5Stack documents [factory restoration](https://docs.m5stack.com/en/guide/cardputer_adv/restore_factory). The existing firmware and settings were overwritten by the test images.
