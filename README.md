# Cardputer Accessibility

Experimental firmware for the **M5Stack Cardputer ADV** with keyboard navigation and offline English eSpeak NG speech. The unified firmware includes a spoken file manager, media playback, voice recording, keyboard learning, USB and Bluetooth keyboard modes, and a PlayPalace client.

- [Download the firmware](https://github.com/willbilec/cardputer-accessibility/releases/latest)
- [Install, update, or recover the firmware](FLASHING.md)
- [Keyboard controls and PlayPalace setup](research/offline-reader/UNIFIED_CONTROLS.md)
- [Build from source and run host tests](BUILDING.md)

On first boot, use the semicolon and period keys to move through Home, Enter to select, and Option + H for spoken help. Alt + Q returns Home. Basic menus and speech work without Wi-Fi or a microSD card. A microSD card is needed for file browsing, recording, and PlayPalace profile storage.

## Research and development

[Project status](PROJECT_STATUS.md) tracks device trials and outstanding checks. [Physical results](research/offline-reader/PHYSICAL_RESULTS.md) record speech and mixed-audio trials with Wi-Fi on and off. [Feasibility notes](research/FEASIBILITY.md) cover the earlier firmware and emulator investigations. The [accessible app guide](ACCESSIBLE_APP_GUIDE.md), [firmware design guide](FUTURE_FIRMWARE_GUIDE.md), and [speech integration guide](TTS_FIRMWARE_GUIDE.md) describe how to extend the firmware.

The device trials cover offline speech, keyboard navigation, Wi-Fi, and simultaneous speech and SD audio. The PlayPalace client has been used in live sessions. Long sessions, reconnect and table recovery, and some first-use flows still need device testing.

## License

Project code is offered under [GPL-3.0](LICENSE). Pinned upstream dependencies and community adaptations retain their respective licenses and notices. See [dependency and experiment notes](research/patches/README.md).
