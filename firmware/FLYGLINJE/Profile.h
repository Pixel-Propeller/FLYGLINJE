#pragma once
#include <Arduino.h>
#include <time.h>
#include "Settings.h"
#include "Net.h"
#include "Flight.h"
#include "Iss.h"
#include "SatMath.h"

// ---------------------------------------------------------------------------
// Tag- und Nachtprofil: Alles, was man an Anzeige, Flugmodus, ISS und Tönen
// einstellen kann, gibt es zweimal. Gewechselt wird nach Uhrzeit oder nach
// Sonnenuntergang/-aufgang am Flug-Standort. Global bleiben: Standort, Abfrage,
// Höhe am Boden ausblenden, Zeitplan, ISS-Mindesthöhe/Sichtbarkeit, Begrüßung.
// ---------------------------------------------------------------------------
namespace Profile {

struct P {
  uint8_t ver = 2;          // 2: Start und Ziel sind getrennte Seiten (0.7.2)
  uint8_t bright = 2;       // 0..7
  bool dark = false;        // Anzeige ganz aus (Taste weckt 15 s)
  bool blink = true;        // Doppelpunkt blinkt
  bool sound = true;        // Tastenton und Signale
  bool flight = false;      // Flugmodus an
  uint8_t radius = 10;
  uint16_t maxAlt = 0;
  uint8_t pages = 0xBF;
  uint8_t maxPlanes = 4;
  uint8_t gap = 10;
  bool bar = true;
  bool iss = false;
  bool issAnnounce = true;
  bool issBeep = false;
};

enum Mode : uint8_t { M_OFF, M_TIME, M_SUN };
inline P p[2];                 // 0 = Tag, 1 = Nacht
inline uint8_t mode = M_OFF;
inline uint16_t from = 22 * 60 + 30, to = 6 * 60 + 30;
inline int active = 0;         // gerade gültiges Profil
inline bool forced = false;    // nach Änderungen sofort neu anwenden

inline void saveProfile(int i) {
  char k[6] = "prof0"; k[4] = '0' + i;
  Settings::prefs.putBytes(k, &p[i], sizeof(P));
}
inline void saveMode() {
  Settings::prefs.putUChar("pmode", mode); Settings::prefs.putUShort("pfrom", from); Settings::prefs.putUShort("pto", to);
}

// Werte des Profils in die laufenden Einstellungen übernehmen (ohne die alten Schlüssel zu ändern)
inline void apply() {
  const P &x = p[active];
  Settings::d.brightness = x.bright;
  Settings::d.colonBlink = x.blink;
  Settings::d.sound = x.sound;
  xSemaphoreTake(Flight::lock, portMAX_DELAY);
  Flight::Config &c = Flight::cfg;
  bool changed = c.enabled != x.flight || c.radiusKm != x.radius || c.maxAltFt != x.maxAlt;
  c.enabled = x.flight; c.radiusKm = x.radius; c.maxAltFt = x.maxAlt; c.pages = x.pages;
  c.maxPlanes = x.maxPlanes; c.clockGapS = x.gap; c.bar = x.bar;
  xSemaphoreGive(Flight::lock);
  if (changed) Flight::refreshNow = true;
  bool issChanged = Iss::cfg.enabled != x.iss;
  Iss::cfg.enabled = x.iss; Iss::cfg.announce = x.issAnnounce; Iss::cfg.beep = x.issBeep;
  if (issChanged) Iss::recompute = true;
}

inline void begin() {
  auto &pr = Settings::prefs;
  bool have = pr.getBytesLength("prof0") == sizeof(P) && pr.getBytesLength("prof1") == sizeof(P);
  if (have) {
    pr.getBytes("prof0", &p[0], sizeof(P));
    pr.getBytes("prof1", &p[1], sizeof(P));
    mode = pr.getUChar("pmode", M_OFF);
    from = pr.getUShort("pfrom", 22 * 60 + 30);
    to = pr.getUShort("pto", 6 * 60 + 30);
  } else {
    // Umstieg von 0.6.x: Tag = bisherige Einstellungen, Nacht = wie der alte Nachtmodus
    P &t = p[0];
    t.bright = Settings::d.brightness; t.blink = Settings::d.colonBlink; t.sound = Settings::d.sound;
    const Flight::Config &c = Flight::cfg;
    t.flight = c.enabled; t.radius = c.radiusKm; t.maxAlt = c.maxAltFt; t.pages = c.pages;
    t.maxPlanes = c.maxPlanes; t.gap = c.clockGapS; t.bar = c.bar;
    t.iss = Iss::cfg.enabled; t.issAnnounce = Iss::cfg.announce; t.issBeep = Iss::cfg.beep;
    p[1] = t;
    p[1].bright = 0;
    p[1].dark = Settings::d.nightMode == 1;
    p[1].flight = t.flight && !p[1].dark;
    p[1].sound = false;
    p[1].issBeep = false;
    mode = Settings::d.nightOn ? M_TIME : M_OFF;
    from = Settings::d.nightFrom; to = Settings::d.nightTo;
    saveProfile(0); saveProfile(1); saveMode();
  }
  // 0.7.0 → 0.7.2: „Route“ war eine Seite, jetzt Start + Ziel
  for (int i = 0; i < 2; i++) if (p[i].ver < 2) {
    if (p[i].pages & (1 << Flight::P_ROUTE)) p[i].pages |= 1 << Flight::P_TO;
    p[i].ver = 2; saveProfile(i);
  }
  if (mode > M_SUN) mode = M_OFF;
  active = 0;
  apply();
}

// Sonnenhöhe am Flug-Standort (Grad)
inline double sunElevationNow() {
  Sat::Observer o; o.set(Flight::cfg.lat, Flight::cfg.lon, 50);
  return Sat::sunElevation(Sat::jdFromUnix((double)time(nullptr)), o);
}

// Nacht = Sonne unter dem Horizont (Sonnenuntergang bis Sonnenaufgang)
inline bool nightNow() {
  if (mode == M_OFF || !Net::timeValid()) return false;
  if (mode == M_TIME) {
    time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
    int m = t.tm_hour * 60 + t.tm_min;
    if (from == to) return false;
    return from < to ? (m >= from && m < to) : (m >= from || m < to);
  }
  static uint32_t at = 0; static bool cached = false;
  if (at == 0 || millis() - at > 30000) { at = millis() | 1; cached = sunElevationNow() < -0.833; }
  return cached;
}

// Nächster Sonnenauf- und -untergang (Unix-Zeit, 0 = keiner in 24 h) – fürs Webinterface
inline void sunTimes(int64_t &rise, int64_t &set) {
  rise = set = 0;
  if (!Net::timeValid()) return;
  Sat::Observer o; o.set(Flight::cfg.lat, Flight::cfg.lon, 50);
  int64_t t0 = (int64_t)time(nullptr);
  double prev = Sat::sunElevation(Sat::jdFromUnix((double)t0), o) + 0.833;
  for (int64_t t = t0 + 120; t <= t0 + 86400 && (!rise || !set); t += 120) {
    double e = Sat::sunElevation(Sat::jdFromUnix((double)t), o) + 0.833;
    if (prev < 0 && e >= 0 && !rise) rise = t;
    if (prev >= 0 && e < 0 && !set) set = t;
    prev = e;
  }
}

// Jede Schleife: Profil wechseln, wenn Tag/Nacht umschlägt
inline bool update() {
  int want = nightNow() ? 1 : 0;
  if (want == active && !forced) return false;
  active = want; forced = false;
  apply();
  return true;
}

inline void changed(int i) { saveProfile(i); if (i == active) forced = true; }

}  // namespace Profile
