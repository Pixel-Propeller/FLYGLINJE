# Firmware

ESP32 firmware for FLYGLINJE, written for the Arduino core for ESP32 **3.0.7**. The web interface lives in `webui/` inside the sketch and is compiled into the firmware gzipped.

> ⏳ **Source code not uploaded yet.** It will land here as `firmware/FLYGLINJE/` together with the final version.

## Layout (planned)

```
firmware/FLYGLINJE/
  FLYGLINJE.ino     entry point
  Config.h          FW_VERSION and build settings
  App.h             screens, intro animation, modes
  Display.h         3 × VK1640, 42 × 9 dot matrix
  Flight.h          flight mode, routes, special aircraft
  Iss.h / SatMath.h ISS passes (own SGP4)
  Alarm.h, Profile.h, Inputs.h, Game.h, Updater.h, Web.h, Diag.h, …
  webui/index.html  web interface source
tools/build_webui.py   gzips webui/ into a header for the firmware
```

## Build

```bash
arduino-cli core install esp32:esp32@3.0.7
python3 tools/build_webui.py
arduino-cli compile \
  --fqbn esp32:esp32:esp32:PartitionScheme=min_spiffs \
  --export-binaries firmware/FLYGLINJE
```

This produces in `firmware/FLYGLINJE/build/esp32.esp32.esp32/`:

| File | Becomes |
|---|---|
| `FLYGLINJE.ino.bin` | `flyglinje-vX.Y.Z.bin` (OTA update) |
| `FLYGLINJE.ino.merged.bin` | `flyglinje-vX.Y.Z-komplett.bin` (first install at `0x0`, trailing `0xFF` cut off) |

## Secrets

Credentials and keys never go into the repository. They are entered in the web interface and stored encrypted on the ESP.

## Releasing

See [docs/RELEASING.md](../docs/RELEASING.md).
