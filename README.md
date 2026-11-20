# Cardputer Accessibility

Accessible firmware for the **M5Stack Cardputer ADV** with keyboard navigation and offline English eSpeak NG speech. The base includes keyboard learning, a spoken file manager, TXT and WAV playback, and settings. Voice Recorder, Bluetooth/USB Keyboard, PlayPalace, and other apps are selectable in the builder.

- [Get the Windows builder or the complete all-app firmware](https://github.com/willbilec/cardputer-accessibility/releases/latest)
- [Choose apps, build, and flash with the Windows or Linux builder](BUILDING.md)
- [Install, update, or recover the firmware](FLASHING.md)
- [Keyboard controls and PlayPalace setup](research/offline-reader/UNIFIED_CONTROLS.md)

On first boot, use the semicolon and period keys to move through Home, Enter to select, and Option + H for spoken help. Alt + Q returns Home. Basic menus and speech work without Wi-Fi or a microSD card. A microSD card is needed for file browsing, recording, and PlayPalace profile storage.

## Research and development

[Project status](PROJECT_STATUS.md) tracks outstanding checks. [Physical results](research/offline-reader/PHYSICAL_RESULTS.md) record speech and mixed-audio trials with Wi-Fi on and off. The [accessible app guide](ACCESSIBLE_APP_GUIDE.md), [firmware design guide](FUTURE_FIRMWARE_GUIDE.md), and [speech integration guide](TTS_FIRMWARE_GUIDE.md) describe how to extend the firmware.

The device trials cover offline speech, keyboard navigation, Wi-Fi, and simultaneous speech and SD audio. The PlayPalace client has been used in live sessions. Long sessions, reconnect and table recovery, and some first-use flows still need device testing.

## License

Project code is offered under [GPL-3.0](LICENSE). Pinned upstream dependencies retain their respective licenses and notices. See the [firmware notices](research/offline-reader/licenses).
