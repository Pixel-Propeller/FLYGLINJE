# Flashing

There are two ways to get FLYGLINJE onto an ESP32.

> **Rule of thumb:** flash the bare ESP32 *before* it gets soldered into the clock. Then you know the board works before you pick up the iron.

## A) First install – complete image (recommended)

Every release contains `flyglinje-vX.Y.Z-komplett.bin`: bootloader, partition table and app merged into one file, written at address `0x0`.

1. Install esptool: `pip install esptool`
2. Connect the ESP32 via USB. If it is already in the clock: **unplug the clock's power cable** first.
3. Erase and flash:
   ```bash
   esptool.py --chip esp32 erase_flash
   esptool.py --chip esp32 write_flash 0x0 flyglinje-vX.Y.Z-komplett.bin
   ```
   If the port isn't found, add `--port /dev/cu.usbserial-XXXX` (macOS), `--port /dev/ttyUSB0` (Linux) or `--port COM3` (Windows). The CH340 driver may be needed on older systems.
4. Reset the ESP. It starts in setup mode.

Browser alternative without installing anything: [ESP Web Flasher](https://espressif.github.io/esptool-js/) (Chrome/Edge) – choose the `.bin`, address `0x0`.

## B) Updates – over the air

Once FLYGLINJE is set up, use **System › Update** in the web interface. The clock fetches the app image (`flyglinje-vX.Y.Z.bin`) from bastelbyra.de. If a new version doesn't boot properly, the ESP rolls back to the previous one and shows a notice.

The complete image is only for USB flashing – the update server ignores files ending in `-komplett`.

## Building from source

See [firmware/README.md](../firmware/README.md).
