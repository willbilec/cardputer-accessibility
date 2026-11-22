---
name: cardputer-firmware-apps
description: Build or debug accessible apps in this Cardputer ADV firmware checkout, especially shared Wi-Fi, asynchronous callbacks, speech, and task scheduling.
---

# Cardputer firmware apps

Use this skill for changes to native apps in the unified Cardputer ADV
firmware. Preserve keyboard-only navigation and offline speech, and keep app
work within the launcher's service and lifecycle model.

## Repository guidance

- Read `AGENTS.md` and follow the project requirements. Before editing, inspect
  `git status` and preserve existing work.
- For an app, follow `ACCESSIBLE_APP_GUIDE.md`; for service ownership and
  scheduling, consult `FUTURE_FIRMWARE_GUIDE.md`; for shared speech details,
  consult `TTS_FIRMWARE_GUIDE.md`.
- Use the registered app `open`, `key`, `tick`, and `close` lifecycle. Keep
  Backspace as global Back and leave physical Alt to shared screen-reader and
  keyboard-layer commands.
- For a complete checkout build, `python builder/build.py <output> --preset all`
  includes catalog apps plus native local apps registered in the ignored
  `builder/local-modules.json` and stored under
  `research/offline-reader/src/local-modules/`. Keep local manifests and app
  sources out of public commits.

## Network and responsiveness

- Use launcher-provided services for saved Wi-Fi credentials. Do not assume
  that `WiFi.reconnect()` loads the launcher's stored SSID and password.
- For persistent WebSocket work, follow NVDA Remote and PlayPalace: give one
  worker pinned to core 0 exclusive ownership of the socket and its `loop()`,
  and exchange bounded incoming events and outgoing commands with the app on
  core 1. Do not call `sendTXT()` from a receive callback. Handle queued events
  and speak from the app loop; copy or acknowledge callback payloads before
  returning when the network library owns their memory.
- For target-device controls, distinguish Wi-Fi or WebSocket connectivity
  from a valid response from the target. Keep menus and commands unavailable
  until that response arrives, and hide them again if the connection is lost.
  Keep the connecting screen separate from the screen restored on reconnect.
- When speech repeats one word or sounds like static, trace repeated calls to
  the speech service before changing audio settings or task placement. Advance
  poll timers even on failures before sending, announce a recurring background
  error once until it clears, and keep explicit user actions audible. Track
  pending requests separately from command IDs so zero can time out.
- For protocols that accept approximate time, use the saved clock snapshot
  plus boot uptime as an app-local fallback while shared network time sync
  runs. Switch to valid system time when available. Keep the estimate out of
  the system clock and certificate checks.
- If selecting one command drops the socket, measure the actual reply size
  with a read-only request and compare it with the compiled transport limit.
  Builder module defines reach app sources. Verify library settings in the
  library build and scope a larger limit to the client that needs it.
- Separate host reachability from device reachability. A host probe can show
  that a printer or service is online while the Cardputer has not connected
  with its saved Wi-Fi settings.
- Treat core pinning as a measured scheduling change, not a default fix for a
  freeze. Locate synchronous work and collect relevant timing/audio/resource
  evidence before adding another task or crossing core boundaries.

Record build success separately from physical Cardputer and assistive
technology acceptance. Ask the user to verify behavior that depends on live
speech, keyboard input or network access when those observations cannot be
made from the host. Use `BUILDING.md` for module selection and help assets;
use `FLASHING.md` for installation.
