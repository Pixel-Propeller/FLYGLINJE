# Firmware

ESP32 firmware for FLYGLINJE, written for the **Arduino core for ESP32 3.0.7**. No extra libraries needed – everything comes with the core.

```
firmware/
  FLYGLINJE/            Arduino sketch (open FLYGLINJE.ino)
    FLYGLINJE.ino       start-up, main loop, rollback protection
    Config.h            version, pins, defaults
    partitions.csv      2 × 1.9 MB app slots for OTA, small SPIFFS, coredump
    webui/index.html    web interface source
    webui/airports.js   airport list for the album (OurAirports)
    WebUI.h             generated – web interface gzipped for the firmware
    Font.h              generated – 5 × 7 pixel font
    …                   see NOTIZEN.md for a file-by-file overview
  tools/
    build_webui.py      webui/ → WebUI.h (gzip + ETag)
    gen_font.py         readable glyph drawings → Font.h
  NOTIZEN.md            technical notes (German, partly from v0.6.2)
```

## Build

### Arduino IDE

1. Boards Manager → **esp32 by Espressif Systems**, version **3.0.7**
2. Open `firmware/FLYGLINJE/FLYGLINJE.ino`
3. Board: **ESP32 Dev Module** – `partitions.csv` in the sketch folder sets the partition layout automatically
4. Upload. If it hangs at „Connecting…“, hold the BOOT button on the ESP.

### arduino-cli

```bash
arduino-cli core install esp32:esp32@3.0.7
python3 firmware/tools/build_webui.py        # only after changing webui/
arduino-cli compile --fqbn esp32:esp32:esp32 --export-binaries firmware/FLYGLINJE
```

Output in `firmware/FLYGLINJE/build/esp32.esp32.esp32/`:

| File | Becomes |
|---|---|
| `FLYGLINJE.ino.bin` | `flyglinje-vX.Y.Z.bin` (OTA update) |
| `FLYGLINJE.ino.merged.bin` | `flyglinje-vX.Y.Z-komplett.bin` (first install at `0x0`, trailing `0xFF` cut off) |

## Changing the web interface

Edit `webui/index.html`, then run `python3 firmware/tools/build_webui.py`. It also refreshes the logo glyphs from the font. Never edit `WebUI.h` by hand.

Changing the pixel font: edit the drawings in `tools/gen_font.py` and run it – it writes `Font.h`.

## First start

- The clock opens the Wi-Fi **FLYGLINJE-xxxx**. Its password is random per clock (8 characters) and scrolls across the display; it is also printed on the serial monitor (115200 baud).
- Connect with a phone – the setup page opens (otherwise `http://192.168.4.1`), choose your home Wi-Fi.
- Then, in your home network: `http://flyglinje.local` (or the IP shown on the display), log in with **admin / admin** and set your own login or skip.
- Holding *Snooze* for 10 s while plugging in resets the login to admin / admin.

## Secrets

No credentials live in the source. Wi-Fi password, setup password and OTA key are entered on the device and stored encrypted (AES-256-GCM); the login is stored as a PBKDF2 hash. Details in [NOTIZEN.md](NOTIZEN.md#sicherheit-ab-040).

## Releasing

See [docs/RELEASING.md](../docs/RELEASING.md).
