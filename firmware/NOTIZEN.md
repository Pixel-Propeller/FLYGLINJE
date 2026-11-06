# FLYGLINJE – Technische Notizen

> Ursprüngliche Firmware-README, geschrieben zu **v0.6.2**. Grundlagen (Sicherheit, Strom, Daten, ISS, Dateien) gelten weiter; was seit 0.7 dazukam – Sammelalbum, Quartett, Wetter, Tasten-Neubelegung u. a. – steht im [CHANGELOG](../CHANGELOG.md). Build-Anleitung: [README.md](README.md).

Flugmodus, Uhr, zwei Wecker, Webinterface, WLAN-Einrichtung, Updates per WLAN (auch automatisch von deiner Webseite) und Hardware-Test für den umgebauten KORTLINJE.

## Einmalig per USB aufspielen

1. Arduino IDE → Boardverwalter → **esp32 von Espressif** (Version 3.x) installieren.
2. Den ganzen Ordner `FLYGLINJE` öffnen (die Datei `FLYGLINJE.ino`).
3. Werkzeuge:
   - Board: **ESP32 Dev Module**
   - Partition Scheme: **Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)** – Pflicht! Die Firmware ist größer als der Standard-Platz (1,2 MB).
   - Port: der CH340-Port
4. Hochladen. Hängt es bei „Connecting…“: BOOT-Taste am ESP halten, bis „Writing…“ erscheint.

Keine zusätzlichen Bibliotheken nötig. Die Datei `partitions.csv` im Ordner sorgt dafür, dass die Aufteilung für OTA immer stimmt.

## Ohne Diode in der VIN-Leitung

USB und Netzkabel der Uhr **nie gleichzeitig**: zum Hochladen den VIN-Stecker am ESP abziehen.
Mit der Diode 1N5817 (Strich zum ESP) dürfen beide gleichzeitig stecken – dann beim Hochladen auch das Netzkabel einstecken.

## Erster Start

- Die Uhr öffnet das WLAN **FLYGLINJE-xxxx**. Das **Passwort ist je Uhr zufällig** (8 Zeichen, ohne 0/O und 1/I) und läuft als Text über die Anzeige; es steht auch im seriellen Monitor (115200 Baud).
- Handy verbinden → die Einrichtungsseite öffnet sich (sonst `http://192.168.4.1`).
- Heimnetz auswählen, Passwort eingeben, verbinden.
- Danach im Heimnetz: `http://flyglinje.local` (oder die IP, die über die Anzeige läuft).
- Anmelden mit **admin / admin**, eigenen Zugang festlegen oder überspringen.

## Updates ab jetzt ohne Kabel

- **Von bastelbyra.de (empfohlen):** `Webseite/firmware/update.php` und den Ordner `Webseite/firmware/files/`
  nach `httpdocs/firmware/` hochladen – neben die bestehende Firmware-Seite (`firmware/index.php` bleibt).
  Die Uhr kennt die Adresse `https://bastelbyra.de/firmware/update.php` schon ab Werk (änderbar unter System › Firmware).
  Neue Version veröffentlichen: in `Config.h` FW_VERSION erhöhen → *Sketch › Kompilierte Binärdatei exportieren* →
  Datei in `flyglinje-v0.4.0.bin` umbenennen → nach `firmware/files/` hochladen. Fertig.
  Die Uhr prüft 1 Minute nach dem Start und dann alle 12 Stunden. „Jetzt prüfen“ geht jederzeit.
  Mit „Nachts automatisch installieren“ spielt sie Updates zwischen 2 und 5 Uhr ein (nie während ein Wecker klingelt).
  Die Prüfsumme (SHA-256) wird beim Herunterladen kontrolliert; HTTPS-Zertifikate werden geprüft.
- **Webinterface:** System › Firmware › .bin-Datei hochladen.
  Die .bin erzeugt die Arduino IDE über *Sketch › Kompilierte Binärdatei exportieren*.
- **Arduino IDE direkt:** erst unter System › Sicherheit „Updates aus der Arduino IDE erlauben“ einschalten (ab Werk aus, weil das IDE-Verfahren nur MD5 nutzt). Dann erscheint die Uhr unter *Werkzeuge › Port* als Netzwerk-Port „flyglinje“. Passwort = dein Zugangspasswort.
- Startet eine neue Version nicht sauber (Absturz in den ersten 30 Sekunden), springt die Uhr automatisch auf die vorherige zurück.

## Sicherheit (ab 0.4.0)

- **WLAN-Passwort, Einrichtungs-Passwort und OTA-Schlüssel** liegen verschlüsselt im Speicher (AES-256-GCM).
  Der Schlüssel entsteht aus einem zufälligen Gerätegeheimnis und der Chip-Kennung. Wer den Speicher ausliest oder auf
  einen anderen ESP kopiert, sieht keine Passwörter im Klartext.
  Ehrliche Grenze: Wer die Uhr in der Hand hat *und* die Firmware auseinandernimmt, kann den Schlüssel nachbauen.
  Vollständig schützt nur die Flash-Verschlüsselung des ESP32 – die brennt eFuses (nicht umkehrbar, USB-Flashen danach
  nur noch verschlüsselt). Für dieses Geschenk bewusst nicht aktiviert.
- **Zugangspasswort** wird nicht gespeichert, nur ein PBKDF2-Hash (HMAC-SHA256, 4096 Runden, Salz). Ein alter Hash aus 0.3.x
  wird beim ersten Anmelden automatisch umgestellt. Nach einer Passwortänderung werden alle anderen Geräte abgemeldet.
- **Einrichtungs-WLAN** mit zufälligem Passwort je Uhr (neu erzeugen unter System › Sicherheit, Werksreset erzeugt auch ein neues).
- Webinterface mit Schutz-Headern (kein Einbetten in fremde Seiten, nur erlaubte Quellen für Karte und Suche),
  Sitzungs-Cookie HttpOnly + SameSite, Sperre nach 5 Fehlversuchen.
- Updates von der Webseite: nur mit gültigem HTTPS-Zertifikat und passender SHA-256-Prüfsumme.

## Strom sparen (ab 0.4.0)

- **Stromsparen** (ab Werk an): WLAN-Modem schläft zwischen den Router-Signalen, Prozessor 160 statt 240 MHz.
  Spart grob die Hälfte des ESP-Stroms. Das Webinterface reagiert dadurch minimal träger.
- **Nachtmodus** (System › Strom & Nacht): von–bis entweder dunkelste Stufe oder Anzeige ganz aus.
  Eine Taste weckt die Anzeige für 15 s (der erste Druck löst nichts anderes aus). Wecker klingeln immer, auch bei
  ausgeschalteter Anzeige. Bei „Anzeige aus“ pausiert der Flugmodus.
- Die meiste Leistung ziehen die LEDs – Helligkeit 1–3 reicht im Zimmer meistens.

## Daten sparen (ab 0.4.0)

- Flugliste wird **gzip-komprimiert** angefordert (rund 5× kleiner) und mit dem Entpacker aus dem ESP32-ROM gelesen.
- Die Verbindung zu adsb.lol **bleibt offen** (Keep-Alive) – der TLS-Aufbau mit Zertifikaten fällt bei den meisten Abfragen weg.
- Radius wird exakt angefragt, Routen 24 h gemerkt, Routen nur abgefragt, wenn die Seite „Route“ an ist.
- Ohne Flugzeuge wird seltener gefragt (45 s, nach längerer Ruhe 90 s). Neue Stufe **„Sparsam“**: 30 s mit, 120 s ohne Flugzeuge.
- Das Webinterface liegt komprimiert in der Uhr (16 statt 57 KB) und wird vom Browser zwischengespeichert;
  das Live-Bild braucht 96 statt 383 Zeichen, der Status wird nur alle 5 s geholt und gar nicht, wenn die Seite im Hintergrund ist.
- Unter System › Strom & Nacht steht, wie viele Daten der Flugmodus seit dem Start geladen hat.

## Flugmodus

Webinterface › **Flug**:
- **Standort:** auf die Karte tippen oder eine Adresse suchen (Karte und Suche laden aus dem Internet über das Handy). Breite/Länge lassen sich auch direkt eintragen.
- **Radius** 1–50 km – der Kreis auf der Karte wächst live mit. **Max. Flughöhe** (oder „alle“), Flugzeuge am Boden ausblenden.
- **Seiten auf der Uhr:** Flugnummer, Route (z. B. DUS>MUC), Flughöhe, Entfernung, Flugzeugtyp, Richtung am Himmel (z. B. „NW 35°“ = nach Nordwesten, 35° über dem Horizont), Geschwindigkeit.
- **Flüge nacheinander** (1–8), **Abfrage** (adaptiv 12–45 s, 15 s, 60 s, sparsam 30–120 s), **Zeitplan** (außerhalb nur Uhr).
- Darunter die Liste der Flüge im Radius, auch als Punkte auf der Karte.

Auf der Matrix: jeder Flug mit allen gewählten Seiten (je 3 s, lange Texte schieben durch), danach 6 s Uhr.
Unterste Zeile: links ein Punkt je Flugzeug, rechts ein Punkt je Info-Seite (ohne Lücken); der aktuelle blinkt (abschaltbar). Notfall-Squawks (7500/7600/7700) erscheinen als eigene Seite.
Statuspunkt rechts unten = Flugmodus aktiv.

Datenquellen (kostenlos, ohne Schlüssel): adsb.lol (Ersatz: airplanes.live), Routen zuerst von adsb.lol (wie auf der adsb.lol-Karte), sonst von adsbdb.com. Ist nur Start oder Ziel bekannt, steht für das andere „--“ (z. B. „DUS>--“), ganz ohne Route „-- > --“.
Ankunftszeiten liefern diese Quellen nicht – dafür gibt es die Seite „Richtung am Himmel“.

## ISS-Überflüge (ab 0.5.0)

Webinterface › Flug › **ISS-Überflüge** (Standort = der aus dem Flugmodus).
- Die Uhr lädt einmal am Tag die Bahndaten der ISS (TLE von celestrak.org, ca. 150 Byte) und berechnet die Überflüge
  der nächsten 48 Stunden selbst (SGP4, am PC gegen die Referenz geprüft: Zeiten auf ±1 s).
- **Sichtbar** = bei dir ist es dunkel (Sonne mehr als 6° unter dem Horizont) und die ISS wird noch von der Sonne angestrahlt.
- **Mindesthöhe** 10–60° über dem Horizont, nur sichtbare oder alle, **Ankündigen**, **Ton**.

Auf der Matrix:
- ab 6 Stunden vorher jede Minute 3 s „[ISS-Symbol] 21:43“
- ab 10 Minuten vorher abwechselnd Countdown („[ISS] 4MIN“, unten läuft ein Balken voll) und Uhr
- während des Überflugs: „SW 35°“ (wohin schauen: Richtung und Höhe), „SW>NO“ (von wo nach wo), „MAX 62°“; unten der Fortschritt

## Spiele (ab 0.6.0)

Webinterface › **Spiel**. Gespielt wird auf der Matrix, das Handy ist der Controller.
- **Snake:** Steuerkreuz antippen oder wischen (am Rechner: Pfeiltasten). Futter blinkt; Rekord wird gespeichert.
- **Pong:** Finger im Feld hoch und runter = Schläger. Gegen die Uhr oder **zu zweit** (zweites Handy öffnet „Spiel“ und wählt „Rechts“). Wer zuerst 5 Punkte hat, gewinnt.
- Die Eingaben laufen über eine dauerhaft offene WebSocket-Verbindung (Port 81), damit keine verloren gehen; der Spielstand kommt von der Uhr zurück. Während eines Spiels schläft das WLAN nicht, danach wieder wie eingestellt. Ohne Eingabe endet ein Spiel nach 90 s; ein klingelnder Wecker beendet es sofort. Töne folgen der Einstellung „Tastenton“.

Der **Hardware-Test** ist jetzt unter System › Hardware-Test.

## Stromverbrauch

System › Gerät zeigt den Verbrauch. Ohne Messgerät ist es eine Schätzung (Prozessor/WLAN-Modus + leuchtende LEDs × Helligkeit, etwa ±30 %).
Mit einem **INA219-Modul** (I²C: SDA → GPIO32, SCL → GPIO33, 3V3, GND; die 5-V-Leitung durch VIN+/VIN− des Moduls) zeigt die Uhr den echten Messwert.

## Tasten

| Taste | kurz | lang (1 s) |
|---|---|---|
| **Schlummern** (oben) | nächsten Wecker zeigen · beim Klingeln: schlummern | beim Einstecken 10 s halten: Zugang auf admin/admin |
| **Helligkeit** (oben) | 8 Stufen durchschalten | – |
| **Modus** (oben) | Datum → IP-Adresse (ohne Heimnetz: Einrichtungs-WLAN + Passwort) → Uhr | Begrüßung zeigen |
| **Wecker/Ton** (oben) | alle Wecker an/aus | Tastenton an/aus |
| **Wecker** (hinten) | W1 / W2 anzeigen | Wecker einstellen |
| **Einstellen** (hinten) | bei angezeigtem Wecker: einstellen | Uhrzeit von Hand stellen (ohne WLAN) |
| **+ / −** (hinten) | Helligkeit · bei angezeigtem Wecker: an/aus · beim Einstellen: Wert ändern (halten = schnell) | |

Einstellen: Stunde → *Einstellen* → Minute → *Einstellen* → Tage (Mo–Fr, täglich, Sa–So, einmalig) → *Einstellen* = speichern.
Nach 20 s ohne Taste wird abgebrochen. Beim Klingeln stoppt jede Taste außer *Schlummern*.
Statuspunkte: links oben = Wecker 1 an, links unten = Wecker 2 an, rechts oben blinkt = schlummert, rechts unten = Flugmodus aktiv.

Noch nicht angeschlossene Tasten lösen nichts aus: eine Tastengruppe wird erst aktiv, wenn sie 2 s lang Ruhespannung (über 2,8 V) hatte.

## Dateien

| Datei | Inhalt |
|---|---|
| `FLYGLINJE.ino` | Start und Hauptschleife, Rollback-Schutz |
| `Config.h` | Version, Pins, Standardwerte |
| `Display.h` | Treiber für die drei VK1640, Bildpunkt-Zuordnung, Text |
| `Font.h` | 5×7-Schrift (mit Ä Ö Ü, °, ♥, ✈) |
| `Inputs.h` | Tasten (Analog) und Summer |
| `Net.h` | WLAN, Einrichtungs-WLAN, mDNS, Zeit, OTA aus der IDE |
| `App.h` | Was die Anzeige zeigt, Tastenbedienung |
| `Alarm.h` | Zwei Wecker, Schlummern, Klingelmuster |
| `Updater.h` | Updates von der eigenen Webseite |
| `Flight.h` | Flugmodus: Abfrage im Hintergrund, Routen, Seiten |
| `Iss.h` | ISS-Überflüge: Bahndaten laden, Überflüge berechnen |
| `SatMath.h` | SGP4-Bahnmodell, Blickrichtung, Sonne, Überflug-Suche (auch am PC testbar) |
| `Power.h` | Stromverbrauch: Schätzung oder INA219-Messung |
| `Game.h` | Snake und Pong auf der Matrix |
| `Diag.h` | Fehlersuche: Neustart-Gründe, „Brotkrumen“ |
| `Fetch.h` | HTTPS-Abruf mit gzip und Keep-Alive |
| `HttpBody.h` | Stückelung und gzip entpacken (auch am PC testbar) |
| `FlightParse.h` | Flugdaten zerlegen (auch am PC testbar) |
| `Web.h` | Webserver und Schnittstelle |
| `WebUI.h` | Webinterface, gzip-komprimiert (erzeugt aus `webui/index.html`) |
| `Settings.h` | Gespeicherte Einstellungen, Verschlüsselung, Passwort-Hash |
