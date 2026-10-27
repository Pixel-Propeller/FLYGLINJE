# Changelog

All notable changes to FLYGLINJE are listed here.
Format based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/), versions follow [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Added
- Pong for two: clearly show which player you are (left/right)

<!-- TODO: Einträge für 0.5.0 – 0.7.0 aus den RELEASES-Notizen in Desktop/GEschenk übernehmen -->

## [0.7.0]

Current version on bastelbyra.de. *Details to be added.*

## [0.4.8]

### Fixed
- Reboots in flight mode: the flight task now runs on core 1, so long TLS handshakes no longer trip the task watchdog on core 0. This also ends the reboot loops and update rollbacks it caused.

### Changed
- Power saving now runs the CPU at 160 MHz instead of 80 MHz.

## [0.4.1] – [0.4.7]

### Added
- Reason of the last reboot plus reboot history (pinpoints where a crash happened)
- Notice after an automatic update rollback
- Snooze per alarm, snooze status dot
- Status dots freely assignable (Test tab)
- Show the clock again after each flight (configurable)

### Changed
- Brightness 1–8 as buttons, centered time fields
- Only one TLS connection at a time, no keep-alive connections
- First flight query 40 s after boot

### Fixed
- Update download aborting early (since 0.4.2)

## [0.4.0] – „Nachtflug“

### Added
- Encrypted secrets (AES-256-GCM), PBKDF2 login, random setup password
- Content Security Policy for the web interface
- Modem sleep and night mode
- Web interface delivered gzipped with ETag caching

### Changed
- Arduino IDE OTA disabled by default

[Unreleased]: https://github.com/Pixel-Propeller/FLYGLINJE/compare/v0.7.0...HEAD
[0.7.0]: https://github.com/Pixel-Propeller/FLYGLINJE/releases/tag/v0.7.0
[0.4.8]: https://github.com/Pixel-Propeller/FLYGLINJE/releases/tag/v0.4.8
