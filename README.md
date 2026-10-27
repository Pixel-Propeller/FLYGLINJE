# FLYGLINJE

FLYGLINJE – A DIY ESP32-powered project. Built with curiosity, questionable engineering decisions, and absolutely no guarantee that everything works as intended.

FLYGLINJE turns the IKEA **KORTLINJE** alarm clock into a **live flight tracker** – without giving up what the clock already does. An ESP32 takes over the three display drivers, the buttons and the buzzer. Everything is set up in the browser; firmware updates arrive over the air.

> **Status:** work in progress. Builds run on real hardware, but expect rough edges.
> Current firmware: **v0.7.0** · Build guide (German): [bastelbyra.de/umbau](https://bastelbyra.de/umbau/)

---

## Features

- **Flight mode** – shows nearby flights on the clock display
- **Clock & alarms** – two alarms, snooze per alarm, night mode, adjustable brightness (1–8)
- **Web interface** – setup, settings, test page with live button voltages, status-dot mapping
- **Over-the-air updates** – the clock fetches new releases from bastelbyra.de; automatic rollback if a new version fails to boot
- **Security by default** – random setup password, PBKDF2 login, AES-256-GCM encrypted secrets, Content Security Policy, IDE-OTA disabled
- **Diagnostics** – reason of the last reboot plus a reboot history pointing to where a crash happened
- **Pong for two** – because why not

## Hardware

| Part | Notes |
|---|---|
| IKEA KORTLINJE alarm clock | the donor |
| ESP32 DevKit (30 pin, ESP-WROOM-32, CH340) | USB-C version recommended |
| Schottky diode 1N5817 | between clock 5 V and ESP `VIN` |
| Thin wire, pin headers, Dupont cables | headers make the ESP removable |
| *optional:* AHT20 / SHT30, DS3231 | temperature/humidity, real-time clock via I²C |

Wiring overview: **[docs/wiring.md](docs/wiring.md)**

## Quick start

1. **Rebuild the clock** following [docs/wiring.md](docs/wiring.md) (detailed photo guide on [bastelbyra.de/umbau](https://bastelbyra.de/umbau/)).
2. **Flash the complete image** from the [latest release](../../releases/latest):
   ```bash
   esptool.py --chip esp32 write_flash 0x0 flyglinje-vX.Y.Z-komplett.bin
   ```
   Details: [docs/flashing.md](docs/flashing.md)
<!-- TODO vor Veröffentlichung prüfen: Name des Einrichtungs-WLANs, wo das Passwort angezeigt wird, IP -->
3. **Connect** to the setup Wi-Fi `FLYGLINJE-xxxx`. The random setup password is shown on the clock display.
4. **Open** `http://192.168.4.1`, enter your Wi-Fi and location – done.

From then on updates are installed from the web interface (System › Update).

## Repository layout

```
firmware/   ESP32 firmware source
web/        web interface sources (served gzipped from the ESP)
docs/       wiring, flashing, release process
hardware/   photos, wiring graphics
```

## Safety

You are opening a mains-powered device and soldering on its board. Unplug it before working on it, measure every wire before switching on, and never connect USB to the ESP while the clock is powered without the diode in `VIN`. You do this at your own risk.

## Credits

This project would not exist without the people who opened the KORTLINJE first. See **[CREDITS.md](CREDITS.md)**.

## License

[MIT](LICENSE) – for the code and documentation in this repository. Third-party material is listed in [CREDITS.md](CREDITS.md).

FLYGLINJE is a hobby project and not affiliated with or endorsed by IKEA. KORTLINJE is a product name of Inter IKEA Systems B.V.

