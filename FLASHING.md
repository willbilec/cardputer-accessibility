# Flashing and recovery

The [desktop builder](BUILDING.md) checks the connected chip and partition
table, then shows USB recovery instructions before it writes. Select a saved
image and choose **Flash selected build**. For a bundle stored elsewhere, use
**Tools → Browse for a saved build** and open its `build.json`.
The builder uses a 1200-baud USB touch to enter download mode from the running
launcher. Press F5 to refresh USB ports after connecting the device. You can
also use the manual procedure below.

Use an **M5Stack Cardputer ADV with 8 MiB flash**, a data-capable USB cable, and Python. The release ZIP contains the bootloader, partition table, OTA initialization data, application, and their SHA-256 checksums. Extract it and run the commands from the extracted directory. Substitute your download port for `PORT` in each command.

```sh
python -m pip install esptool==5.3.0
python -m serial.tools.list_ports -v
```

Verify the extracted flash files against the supplied checksums before installing.

```sh
python -c "from hashlib import sha256; from pathlib import Path; entries=[line.split() for line in Path('SHA256SUMS').read_text().splitlines()]; mismatches=[name for digest,name in entries if sha256(Path(name).read_bytes()).hexdigest()!=digest]; raise SystemExit('Checksum mismatch: '+', '.join(mismatches) if mismatches else 0)"
```

## Enter download mode

Close serial monitors and unplug USB. Switch the Cardputer ADV off. Press and
hold G0, the button on the back edge just to the right of the power switch.
Keep it held while connecting the device with a USB-C data cable. Release G0
after USB powers the device, then list ports again. The download port may
differ from the application port. On Windows it is typically named `COM4` or
similar. On Linux it is typically `/dev/ttyACM0`.

```sh
python -m esptool --chip esp32s3 --port PORT --before no-reset --after no-reset flash-id
```

Confirm the tool detects 8 MB flash. The application and download modes can use different port names. [M5Stack download-mode and recovery guide](https://docs.m5stack.com/en/guide/cardputer_adv/restore_factory)

## First installation or migration from another partition layout

This replaces the bootloader, partition table, OTA selection, and application. It can make previous firmware or settings inaccessible. Back up the microSD separately and remove it while flashing. If you need to preserve the installed firmware and settings, first save and verify a complete read of flash. Keep any backup private because it can contain credentials.

```sh
python -m esptool --chip esp32s3 --port PORT --before no-reset --after no-reset read-flash 0x0 0x800000 original-flash.bin
```

Run the install command after confirming the target. Esptool verifies written data. Power-cycle the device if it stays in download mode after the command finishes.

```sh
python -m esptool --chip esp32s3 --port PORT --before no-reset --after watchdog-reset write-flash --flash-size 8MB 0x0 bootloader.bin 0x8000 partitions.bin 0xE000 boot_app0.bin 0x10000 firmware.bin
```

The reader app starts at `0x10000` and has `0x7E0000` bytes. The installation writes only the four regions listed above. [Espressif esptool command reference](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html)

## App-only update from the same layout

Verify the installed partition digest against the build before writing only the application. Continue only after verification succeeds.

```sh
python -m esptool --chip esp32s3 --port PORT --before no-reset --after no-reset verify-flash 0x8000 partitions.bin
python -m esptool --chip esp32s3 --port PORT --before no-reset --after watchdog-reset write-flash --flash-size 8MB 0x10000 firmware.bin
```

## First boot and recovery

After reboot, press period to hear the next Home item or Option + H for menu help. Use the [controls guide](research/offline-reader/UNIFIED_CONTROLS.md) for SD, Wi-Fi, and PlayPalace setup. The optional PlayPalace sound bank can be generated from a local copy of the desktop client sounds with [the conversion script](research/offline-reader/prepare_playpalace_effects.py) and FFmpeg. Run the script with the sounds directory and an output path of `PlayPalace/effects.ppfx` on the SD card.

If the device fails to boot, re-enter download mode with G0. Check the USB cable, serial port and esptool output, then retry with the four files from the same release. To return to the vendor firmware, follow [M5Stack's factory restore procedure](https://docs.m5stack.com/en/guide/cardputer_adv/restore_factory).
