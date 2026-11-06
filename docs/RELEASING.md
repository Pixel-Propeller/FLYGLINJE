# Release-Ablauf (für Maintainer)

Checkliste für jede neue Version. Version im Format `vMAJOR.MINOR.PATCH`, z. B. `v0.8.0`.

## 1. Version vorbereiten

- [ ] `firmware/FLYGLINJE/Config.h`: `#define FW_VERSION  "X.Y.Z"`
- [ ] Webseite `inc/firmware.php`: neuen Block oben in `RELEASES` (Version, Datum, Spitzname, Punkte)
- [ ] Webseite `inc/config.php`: `FW_VERSION` und `FW_DATE`
- [ ] Changelog neu erzeugen:
  ```bash
  python3 tools/changelog_from_site.py ~/Desktop/GEschenk/inc/firmware.php > CHANGELOG.md
  ```
- [ ] Handbuch prüfen, falls sich Webinterface oder Tasten geändert haben (Handbuch folgt dem Webinterface)

## 2. Bauen

Siehe [firmware/README.md](../firmware/README.md) (nach Änderungen am Webinterface vorher `python3 firmware/tools/build_webui.py`). Es entstehen **drei** Dateien:

| Datei | Inhalt | Wofür |
|---|---|---|
| `flyglinje-vX.Y.Z.bin` | nur die App (`FLYGLINJE.ino.bin`) | OTA-Update über bastelbyra.de |
| `flyglinje-vX.Y.Z-komplett.bin` | `FLYGLINJE.ino.merged.bin`, FF-Ende auf 4-KB-Grenze abgeschnitten | Erstinstallation per USB an `0x0` |
| `flyglinje-vX.Y.Z.txt` | ein Satz, was neu ist | Update-Hinweis in der Uhr |

FF-Ende abschneiden:

```bash
python3 - <<'PY'
d = open("FLYGLINJE.ino.merged.bin", "rb").read()
e = (len(d.rstrip(b"\xff")) + 4095) // 4096 * 4096
open("flyglinje-vX.Y.Z-komplett.bin", "wb").write(d[:e])
PY
```

Prüfsummen:

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
- Anhänge: `flyglinje-vX.Y.Z.bin`, `flyglinje-vX.Y.Z-komplett.bin`, `SHA256SUMS.txt` (die `.txt` nicht – die ist nur für die Uhr)

Mit der GitHub-CLI geht alles in einem Schritt:

```bash
gh release create vX.Y.Z flyglinje-vX.Y.Z.bin flyglinje-vX.Y.Z-komplett.bin SHA256SUMS.txt \
  --title "FLYGLINJE vX.Y.Z" --notes-file release-notes.md
```

## 6. Webseite bastelbyra.de

- [ ] `firmware/files/` – beide `.bin` und die `.txt` hochladen, alte Version nach `inc/_backup-…/alte-firmware/`
- [ ] `inc/config.php`, `inc/firmware.php`, ggf. `inc/code-snippets.php` hochladen
- [ ] Sobald das Repo öffentlich ist: in `inc/config.php` `GITHUB_URL = 'https://github.com/Pixel-Propeller/FLYGLINJE'` setzen – dann erscheinen die GitHub-Links auf der Seite

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
