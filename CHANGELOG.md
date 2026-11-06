# Changelog

Alle Versionen der FLYGLINJE-Firmware. Jede Version hat einen Spitznamen aus der Fliegerei.
Erzeugt aus der Versionsliste auf [bastelbyra.de/firmware](https://bastelbyra.de/firmware/) mit `tools/changelog_from_site.py` – bitte dort pflegen, nicht hier.

## [Unreleased]

## [0.8.5] – 2026-11-05 · „Quartett“

- Flugzeug-Quartett zu zweit: gespielt wird mit den Flugzeugtypen aus dem eigenen Sammelalbum (ab 10 gesammelten Typen), zufällig gemischt, die Kartenzahl pro Spieler ist wählbar
- Spieler 2 tritt wie bei Pong über die Anmeldeseite bei, jedes Handy sieht nur die eigene Karte. Wer dran ist, tippt einen Wert an – der höhere gewinnt beide Karten, bei Gleichstand wandern sie in den Topf
- Die Uhr verteilt und vergleicht die Karten selbst und zeigt den Stand auf der Punktmatrix

## [0.8.4] – 2026-11-05 · „Lounge“

- Neue Info-Fenster für Typen, Airlines und Flughäfen: großes Foto, darauf Code und Name, Merkmale als farbige Plaketten (Seltenheit, Allianz, Flughafengröße, Silber/Gold)
- „Deine Karte“ als kleines Ticket mit Abrisskante, Fakten als ruhige Liste, Quartett-Balken im Farbverlauf der Seltenheit

## [0.8.3] – 2026-11-05 · „Boarding“

- Das Sammelalbum im Bordkarten-Look: großer Code, Kopfzeile in der Farbe der Seltenheit, Abrisskante und Stempel für Silber und Gold
- Filter als kompakte Leiste zum Wischen, die Seltenheiten mit farbigem Punkt
- Das Kreuz zum Schließen bleibt beim Scrollen im Steckbrief sichtbar, das Fenster passt auch mit den Safari-Leisten
- Die Karte im Flug-Tab liegt nicht mehr über der Navigation

## [0.8.2] – 2026-11-05 · „Bordkarte“

- Airlines im Album lassen sich antippen: Foto, Kurzporträt, Funkrufname und Fakten wie Gründung, Sitz, Drehkreuze, Allianz, Flottengröße, Passagiere pro Jahr und Vielfliegerprogramm – für alle 87 Airlines im Katalog
- Flughafen-Fenster mit mehr Fakten aus Wikipedia: Eröffnung, Betreiber, Passagiere, Fracht, Flugbewegungen, Terminals, Beschäftigte, Fläche und Start- und Landebahnen samt der längsten

## [0.8.1] – 2026-11-05 · „Ankunftstafel“

- Flughafen-Karten im Album lassen sich antippen: Foto und Kurzbeschreibung aus Wikipedia, dazu ICAO-Code, Höhe, Größe und wie weit der Flughafen von dir entfernt ist
- Deine Karte zeigt, wann der Flughafen zum ersten und zuletzt als Start oder Ziel auftauchte und wie oft

## [0.8.0] – 2026-11-05 · „Abflughalle“

- Flughäfen im Album zeigen jetzt, was hinter dem Code steckt: Stadt, Name und Land – BER ist Berlin Brandenburg, LHR London Heathrow. Über 5.000 Flughäfen weltweit, die Liste lädt erst, wenn man sie braucht
- Frachter haben im Quartett 0 Sitzplätze – Beluga, Antonow An-124, Il-76 und Co.
- Neu im Quartett: der Boeing Dreamlifter

## [0.7.9] – 2026-11-05 · „Quartett“

- Jede Typ-Karte hat jetzt Quartett-Werte: Länge, Spannweite oder Rotordurchmesser, Startgewicht, Geschwindigkeit, Reichweite, Sitzplätze, Gipfelhöhe und Triebwerke – für 171 Typen, passend zur jeweiligen Variante (737-800 ist nicht 737 MAX 8)
- Zu jedem Wert der Platz unter allen Typen im Katalog, die vordersten 10 % sind Trümpfe mit Stern
- Fotos im Steckbrief laden jetzt: die Uhr hat die Bilder von Planespotters.net und Wikimedia bisher selbst blockiert
- Neue Karten erscheinen nur noch im Webinterface und laufen nicht mehr über die Anzeige

## [0.7.8] – 2026-11-05 · „Steckbrief“

- Antippen einer Karte im Album öffnet einen Steckbrief: ein echtes Foto des Flugzeugs, das du zuerst gesehen hast (Planespotters.net, mit Namen des Fotografen), sonst ein Bild aus Wikipedia
- Darunter die spannendsten Daten aus Wikipedia: Hersteller, Erstflug, im Dienst seit, wie viele gebaut wurden
- Das Album sammelt jetzt Typen, Airlines und Flughäfen – die Liste einzelner Flugzeuge fällt weg und spart Speicher

## [0.7.7] – 2026-11-05 · „Sammelalbum“

- Sammelalbum nach dem Vorbild von Skycards: 176 Flugzeugtypen und 87 Airlines zum Sammeln, dazu jeder Flughafen aus den Routen und jedes einzelne Flugzeug mit Datum
- Fünf Seltenheitsstufen von gewöhnlich bis legendär, Silber ab 10× und Gold ab 50× gesehen
- Neue Karten laufen über die Uhr („NEUE KARTE: AIRBUS A380 · EPISCH!“), seltene mit Fanfare
- Drei neue Abzeichen: Sammler, Kenner und Legendär

## [0.7.6] – 2026-11-05 · „Abholservice“

- Flug verfolgen: eine Flugnummer eintragen, die Uhr findet den Flug weltweit, zeigt alle paar Minuten, wann er landet, und meldet die Landung mit einem Ton
- Abzeichen: 28 kleine Erfolge zum Sammeln – vom ersten Flug über die A380 bis zum Frühaufsteher. Neue laufen mit einer Fanfare über die Anzeige
- Geburtstagsständchen: am besonderen Tag einmal „Happy Birthday“ mit Konfetti auf der Punktmatrix
- Unwetterwarnungen des Deutschen Wetterdienstes für den Standort, ab einstellbarer Warnstufe

## [0.7.5] – 2026-11-05 · „Doppelpass“

- Pong startet nicht mehr sofort: erst 1 oder 2 Spieler wählen
- Zu zweit wartet die Uhr auf Spieler 2 („WARTE AUF SPIELER 2“). Auf der Startseite der Uhr erscheint dann „Spieler 2 – mitspielen“ – ohne Anmeldung, das zweite Handy steuert nur den rechten Schläger

## [0.7.4] – 2026-11-05 · „Bordservice“

- „Schlummern“ ist jetzt das Modus-Menü: Flug, Wetter, ISS, Wecker, Ton und Timer durchschalten, hinten + und − schalten an und aus
- „Modus“ zeigt der Reihe nach Datum, Wetter, Flüge von heute und die Begrüßung
- Küchentimer: per Webinterface oder Taste, Sanduhr mit Restzeit auf der Anzeige, am Ende piept es
- Besondere Tage: eigener Text am Tag selbst (mit Herzen), vorher auf Wunsch ein Countdown
- Wetter nach dem Wecker – bei viel Regen mit Schirm-Hinweis – und ein Lichtwecker, der die Anzeige vorher langsam aufhellt
- Meldung „Über dir!“, wenn ein Flugzeug fast senkrecht über dem Haus ist
- Flugstatistik: Flüge heute, gestern und insgesamt, Rekorde und die besonderen Flugzeuge der letzten 7 Tage
- Einstellungen als Datei sichern und zurückspielen (ohne WLAN und Zugangsdaten)
- Pong zu zweit: das Handy zeigt groß die eigene Seite, auf der Uhr blinkt der eigene Schläger
- Eine geheime Nachricht für Eingeweihte
- Behoben: Die Temperatur stand als „12L“ statt „12°C“ da

## [0.7.3] – 2026-11-05 · „Wetterlage“

- Wettermodus: Temperatur und Regenwahrscheinlichkeit am Standort des Radars, in einstellbarem Abstand kurz zwischen der Uhrzeit (Daten: Open-Meteo)
- Neue Symbole nach dem Vorbild der Flughafen-Schilder: Start und Ziel zeigen Abflug bzw. Ankunft über der Bahn
- Flugmodus an/aus per Taste mit Symbol: Abheben = an, Landen = aus
- Zu lange Einblendungen wie „WECKER AUS“ laufen jetzt einmal komplett durch

## [0.7.2] – 2026-11-05 · „Notlandung“

- Tasten oben neu belegt: „Modus“ kurz zeigt Datum und Begrüßung, die IP-Adresse erst bei 1 s Halten; „Schlummern“ 1 s halten schaltet den Flugmodus an oder aus
- Start und Ziel als eigene Seiten, rechtsbündig der Flughafen – „--“ nur für das, was wirklich unbekannt ist
- Snake und Pong ohne Hänger: ein schlafendes Handy kann die Uhr nicht mehr sekundenlang anhalten, während eines Spiels ruht der Flugabruf
- Routen kommen schneller und zuverlässiger, auch wenn eine Quelle gerade pausiert
- Besondere Flugzeuge: Notfall, Funkausfall, Entführung, Rettungsflug, Militär, A380, Jumbo, Beluga und mehr bekommen eine eigene Seite – Notfälle blinken, stehen vorne und melden sich mit einem Ton
- Test: Beispielflüge für alle besonderen Fälle direkt auf der Uhr

## [0.7.0] – 2026-10-26 · „Nachtschicht“

- Beim Start fliegt ein Flugzeug über die Anzeige, danach kommt die Begrüßung
- Tag- und Nachtprofil: Helligkeit, Anzeige aus, Töne, Flugmodus und ISS getrennt einstellbar, Wechsel nach Uhrzeit oder Sonnenuntergang
- Flugdetails im Webinterface: Flug antippen für Route, Kennzeichen, Höhe, Tempo, Richtung und Kartenlink
- Status-Lichter erklärt: unter der Live-Anzeige steht, wofür AM, PM, AL1, Zz und AL2 gerade stehen
- Schonenderer Abruf: Pausen nach Fehlern oder „zu vielen Anfragen“, Abrufe pro Stunde und letzte Fehler sichtbar
- Mehr Sicherheit: Webinterface nur aus dem eigenen WLAN, Anmeldung 7 Tage gültig, wachsende Sperre bei falschem Passwort

## [0.6.3] – 2026-10-25 · „Amperemeter“

- System: Stromstärke groß in Ampere, darunter die Rechnung Schritt für Schritt

## [0.6.2] – 2026-10-25 · „Bodenkontrolle“

- Update-Fortschritt in Prozent im Webinterface, danach geht es automatisch zur Anmeldung
- Kurze Texte stehen still in der Mitte, lange laufen durch
- Pong: Schläger reagiert zuverlässiger

## [0.6.1] – 2026-10-25 · „Joystick“

- Spiele reagieren sofort und verlieren keine Eingaben mehr
- Pong: Mitte nur noch oben und unten markiert

## [0.6.0] – 2026-10-25 · „Spielpause“

- Snake und Pong auf der Punktmatrix – das Handy ist der Controller
- Pong gegen die Uhr oder zu zweit mit zwei Handys
- Snake-Rekord wird gespeichert
- Hardware-Test jetzt unter System

## [0.5.2] – 2026-10-25 · „Zwei Quellen“

- Routen zuerst von adsb.lol, sonst von adsbdb.com
- Nur Start oder nur Ziel bekannt: das andere als „--“, ganz ohne Route „-- > --“
- Im Webinterface steht, woher die Routen kamen

## [0.5.1] – 2026-10-25 · „Kurskorrektur“

- Routen von adsb.lol (eine Anfrage für alle Flugzeuge)

## [0.5.0] – 2026-10-25 · „Raumstation“

- ISS-Überflüge: Uhrzeit vorab, Countdown ab 10 Minuten, beim Überflug Blickrichtung und Höhe
- Berechnet die Bahn selbst – nur ca. 150 Byte Bahndaten am Tag
- Stromverbrauch im Webinterface (geschätzt oder mit INA219 gemessen)
- Punkte im Flugmodus ohne Lücken – Platz für viele Flugzeuge

## [0.4.10] – 2026-10-25 · „Gleitpfad“

- Flugmodus: unten ein Punkt je Flugzeug und je Info, der aktuelle blinkt

## [0.4.8] – 2026-10-25 · „Zweites Triebwerk“

- Flugmodus startet die Uhr nicht mehr neu
- Flugabfrage läuft auf dem zweiten Prozessorkern

## [0.4.7] – 2026-10-23 · „Flugschreiber“

- Die Uhr merkt sich, wann und wobei sie neu gestartet ist
- Flugmodus und Updates kommen sich nicht mehr in die Quere
- Zurückgenommene Updates werden im Webinterface gemeldet

## [0.4.6] – 2026-10-23 · „Ruhige Luft“

- Flugmodus deutlich stabiler
- Nach jedem Flug zeigt die Uhr wieder die Uhrzeit (Dauer einstellbar)
- Fehlersuche: Speicher und Flugabfragen im Webinterface

## [0.4.5] – 2026-10-23 · „Feinschliff“

- Helligkeit mit acht Stufen zum Antippen
- Uhrzeiten genau mittig in ihren Feldern
- Statuspunkte frei zuordenbar – Wecker 1 und 2 leuchten jetzt am richtigen Punkt

## [0.4.4] – 2026-10-23 · „Landeanflug“

- Nach einem Update: „Update auf Version …“, dann die Begrüßung, dann die Uhr
- IP-Adresse läuft nur noch nach dem Einrichten des WLANs durch

## [0.4.3] – 2026-10-23 · „Schlummertaste“

- Schlummern pro Wecker an- oder abschaltbar
- Beim Schlummern leuchtet rechts der mittlere Punkt
- Oberfläche für schmale Handys aufgeräumt

## [0.4.2] – 2026-10-23 · „Durchstart“

- Updates von bastelbyra.de werden jetzt vollständig geladen
- Fehler beim Update erscheinen im Webinterface
- Flugmodus pausiert während eines Updates

## [0.4.1] – 2026-10-23 · „Blackbox“

- Zeigt im Webinterface, warum die Uhr zuletzt neu gestartet ist
- Beide aktiven Wecker auf einen Blick
- Abgebrochene Anfragen werden einmal neu versucht

## [0.4.0] – 2026-10-23 · „Nachtflug“

- WLAN-Passwort und andere Geheimnisse verschlüsselt gespeichert (AES-256)
- Zugangspasswort als PBKDF2-Hash, eigenes Einrichtungs-Passwort je Uhr
- Nachtmodus: dunkel oder Anzeige aus, Wecker klingeln trotzdem
- Stromsparen: WLAN-Schlaf und 80 MHz
- Flugdaten gzip-komprimiert über eine offene Verbindung – rund 5× weniger Daten

## [0.3.0] – 2026-10-23 · „Erstflug“

- Flugmodus: Flüge im Umkreis mit Flugnummer, Route, Höhe, Entfernung, Typ und Richtung
- Standort per Karte, Radius als Kreis, Zeitplan
- Zwei Wecker mit Wochentagen, Schlummern und Weckspruch
- Updates direkt von bastelbyra.de, auf Wunsch nachts automatisch

## [0.0.1] – 2026-10-15 · „Lötkolben zum ersten Mal an“

- Erste Pixel leuchten
- Ein Pixel leuchtet an der falschen Stelle

[Unreleased]: https://github.com/Pixel-Propeller/FLYGLINJE/compare/v0.8.5...HEAD
[0.8.5]: https://github.com/Pixel-Propeller/FLYGLINJE/releases/tag/v0.8.5
