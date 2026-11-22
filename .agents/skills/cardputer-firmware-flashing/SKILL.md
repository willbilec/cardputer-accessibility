---
name: cardputer-firmware-flashing
description: Build and install firmware for this Cardputer ADV checkout, including full module sets, routine app updates, and verified recovery procedures.
---

# Cardputer firmware build and flash

Use this skill to prepare a firmware image or install it on a Cardputer ADV.
Follow `AGENTS.md`, `BUILDING.md` and `FLASHING.md`; those files own the
current project-specific commands and supported layouts.

## Build

- Inspect the working tree and preserve existing changes. Select the exact
  requested module set, help assets and screen mode. For a complete checkout
  build, `python builder/build.py <output> --preset all` includes catalog apps
  and checkout-local native apps registered in the ignored
  `builder/local-modules.json` with sources under
  `research/offline-reader/src/local-modules/`. Do not publish those local
  manifests or sources.
- Use the builder's pinned dependencies and retain its manifest and image
  hashes. Distinguish a successful host build from physical device acceptance.

## Routine installation

- When starting from a running launcher, use the documented 1200-baud touch
  before esptool. Match the new download port to the selected USB device and
  verify its MAC before writing.
- Compare the installed partition table digest in place. For a matching
  layout, write only the app image at `0x10000`; do not read or back up full
  flash for a routine update. Treat a partition mismatch as a separate
  migration that needs its documented recovery path.
- Verify the written app image. After reboot, verify the reported firmware
  version and module set. If the build includes help files, leave the SD card
  inserted and verify the copied files.

## Reporting

Report build, flash, post-boot identity and help-copy results separately.
Call out live keyboard, screen-reader, speech or peripheral checks that still
need the user because they cannot be established by the host flash process.
