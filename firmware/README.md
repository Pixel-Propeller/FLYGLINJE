# Firmware

ESP32 firmware for FLYGLINJE (Arduino core for ESP32 3.x).

<!-- TODO: Quellcode hier ablegen und Build-Anleitung ergänzen (Arduino IDE oder PlatformIO, Board, Partitionsschema, benötigte Bibliotheken) -->

## Build

- Board: ESP32 Dev Module
- Arduino core for ESP32: 3.0.x
- Partition scheme: *to be documented*

## Secrets

Credentials and keys never go into the repository. Copy `secrets.example.h` to `secrets.h` (ignored by git) if the build needs local values.

## Releasing

See [docs/RELEASING.md](../docs/RELEASING.md).
