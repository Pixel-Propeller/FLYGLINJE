# Credits

FLYGLINJE knows nothing by itself. Everything that runs across the display comes from open services and from people who share what they know. Thank you all!

## Hardware research

| Source | What we use | License |
|---|---|---|
| [kobebeefg/ikea-clock-lab](https://github.com/kobebeefg/ikea-clock-lab) | How the KORTLINJE works inside: display-driver pads, pinout, buttons, the cut-and-desolder method | none published – **used as a reference only**, no code, text or photos copied |
| [VIPnytt/Kortlinje](https://github.com/VIPnytt/Kortlinje) (wiki) | Connector pinouts (TOP-KEY, POWER, BUZ), USB cable colours, further board signals, LED map | MIT |

All wiring graphics and descriptions in this repository are our own.

## Flight data

| Source | Used for | Terms |
|---|---|---|
| [adsb.lol](https://adsb.lol) | aircraft nearby (main source) and routes by callsign | ODbL 1.0 |
| [airplanes.live](https://airplanes.live) | fallback when adsb.lol doesn't answer | non-commercial use only |
| [vradarserver/standing-data](https://github.com/vradarserver/standing-data) | the route collection behind adsb.lol routes | see repository |
| [adsbdb](https://www.adsbdb.com) | second opinion for routes | see website |

## Album, weather & space

| Source | Used for | Terms |
|---|---|---|
| [OurAirports](https://ourairports.com) | airport names, cities, countries | Public Domain |
| [Planespotters.net](https://www.planespotters.net) | aircraft photos in the album (photographer named and linked, loaded live) | rights stay with the photographers |
| [Wikipedia / Wikimedia Commons](https://de.wikipedia.org) | facts, short descriptions, top-trumps values, fallback images | texts CC BY-SA 4.0, images as stated per file |
| [Open-Meteo](https://open-meteo.com) | weather mode | CC BY 4.0 |
| [Bright Sky](https://brightsky.dev) / Deutscher Wetterdienst | severe-weather warnings | data: Deutscher Wetterdienst |
| [CelesTrak](https://celestrak.org) | ISS orbital elements (TLE, NORAD 25544) | see website |
| SGP4 – Vallado et al. 2006, [„Revisiting Spacetrack Report #3“](https://celestrak.org/publications/AIAA/2006-6753/) | orbit model, re-implemented and checked against python-sgp4 and Skyfield | – |

## Map & time

| Source | Used for | License |
|---|---|---|
| [OpenStreetMap](https://www.openstreetmap.org/copyright) | map tiles in the web interface | © OpenStreetMap contributors, ODbL |
| [Nominatim](https://nominatim.org) | place search | OSM usage policy |
| [Leaflet](https://leafletjs.com) | the map component | BSD-2-Clause |
| [NTP Pool](https://www.ntppool.org), time.cloudflare.com | time sync | – |

## Software

| Project | License |
|---|---|
| [Arduino core for ESP32](https://github.com/espressif/arduino-esp32) 3.0.7 | LGPL-2.1 |
| [ESP-IDF](https://github.com/espressif/esp-idf) (Wi-Fi, mbedTLS, OTA) | Apache-2.0 |

## Own work

- 5 × 7 pixel font of the clock, including the ISS symbol and airport-style departure/arrival icons – drawn by hand
- Inspiration for the collection album: Skycards – idea only, no material used

FLYGLINJE is a private hobby project. IKEA and KORTLINJE are trademarks of Inter IKEA Systems B.V.; this project is not affiliated with, supported or reviewed by IKEA. All flight data comes from volunteers and may be incomplete or wrong – please keep looking out of the window for navigation.
