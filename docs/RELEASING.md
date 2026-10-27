# Release-Ablauf (für Maintainer)

Checkliste für jede neue Version. Version im Format `vMAJOR.MINOR.PATCH`, z. B. `v0.8.0`.

## 1. Version vorbereiten

- [ ] Versionsnummer in der Firmware erhöhen <!-- TODO: Datei/Konstante eintragen, sobald der Code im Repo ist -->
- [ ] `CHANGELOG.md`: Abschnitt `[Unreleased]` in `[X.Y.Z] – JJJJ-MM-TT` umbenennen, neuen leeren `[Unreleased]` darüber anlegen, Vergleichslinks unten ergänzen
- [ ] Handbuch/Webseite anpassen, falls sich Webinterface oder Tasten geändert haben (Handbuch folgt dem Webinterface)

## 2. Bauen

Es entstehen **zwei** Dateien:

| Datei | Inhalt | Wofür |
|---|---|---|
| `flyglinje-vX.Y.Z.bin` | nur die App | OTA-Update über bastelbyra.de |
| `flyglinje-vX.Y.Z-komplett.bin` | Bootloader + Partitionen + App | Erstinstallation per USB (`0x0`) |

Komplett-Image zusammenfügen (Offsets für Arduino-Core ESP32 mit Standard-Partitionen – bei eigenem Partitionsschema anpassen):

```bash
esptool.py --chip esp32 merge_bin -o flyglinje-vX.Y.Z-komplett.bin \
  --flash_mode dio --flash_size 4MB \
  0x1000  bootloader.bin \
  0x8000  partitions.bin \
  0xe000  boot_app0.bin \
  0x10000 flyglinje-vX.Y.Z.bin
```

Ohne `--fill-flash-size` bleibt das FF-Ende weg, die Datei ist so klein wie möglich.

Prüfsummen erzeugen:

```bash
shasum -a 256 flyglinje-vX.Y.Z*.bin > SHA256SUMS.txt
```

## 3. Testen

- [ ] Komplett-Image auf ein gelöschtes ESP flashen → Einrichtung läuft durch
- [ ] OTA-Update von der Vorversion → startet, kein Rollback
- [ ] System › Letzte Neustarts: keine unerwarteten Neustarts nach einigen Stunden Flugmodus

## 4. Ins Repo

```bash
git add -A
git commit -m "Release vX.Y.Z"
git tag -a vX.Y.Z -m "FLYGLINJE vX.Y.Z"
git push origin main --tags
```

Die `.bin`-Dateien kommen **nicht** ins Repo (stehen in `.gitignore`), sondern an das GitHub-Release.

## 5. GitHub-Release

Auf GitHub: **Releases › Draft a new release** → Tag `vX.Y.Z` wählen.

- Titel: `FLYGLINJE vX.Y.Z – <Spitzname>`
- Text: den Abschnitt aus `CHANGELOG.md` einfügen (Vorlage unten)
- Anhänge: `flyglinje-vX.Y.Z.bin`, `flyglinje-vX.Y.Z-komplett.bin`, `SHA256SUMS.txt`

Mit der GitHub-CLI geht alles in einem Schritt:

```bash
gh release create vX.Y.Z flyglinje-vX.Y.Z.bin flyglinje-vX.Y.Z-komplett.bin SHA256SUMS.txt \
  --title "FLYGLINJE vX.Y.Z" --notes-file release-notes.md
```

## 6. Webseite bastelbyra.de

- [ ] `firmware/files/` – beide `.bin` hochladen
- [ ] `inc/config.php` und `inc/firmware.php` aktualisieren
- [ ] einmalig: `firmware/update.php` und `firmware/files/.htaccess`
- [ ] „Alle Releases“-Link zeigt auf `https://github.com/Pixel-Propeller/FLYGLINJE/releases` (erst sinnvoll, wenn das Repo öffentlich ist)

---

## Vorlage Release-Text

```markdown
## Neu
- …

## Verbessert
- …

## Behoben
- …

### Installation
- **Bestehende Uhr:** Webinterface › System › Update
- **Neue Uhr:** `flyglinje-vX.Y.Z-komplett.bin` an Adresse `0x0` flashen – siehe [docs/flashing.md](https://github.com/Pixel-Propeller/FLYGLINJE/blob/main/docs/flashing.md)
```
