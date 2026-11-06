#pragma once
#include <Arduino.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Config.h"
#include "Settings.h"
#include "Net.h"
#include "Fetch.h"
#include "Diag.h"
#include "SatMath.h"

// ---------------------------------------------------------------------------
// ISS-Überflüge: Die Uhr holt einmal am Tag die Bahndaten der ISS (TLE, ~150 Byte,
// celestrak.org) und berechnet daraus selbst, wann die Raumstation über den
// Standort (der aus dem Flugmodus) zieht – inkl. „sichtbar“: Himmel dunkel und
// ISS noch im Sonnenlicht. Gerechnet wird im Hintergrund-Task (Kern 1).
// ---------------------------------------------------------------------------
namespace Iss {

struct Config {
  bool enabled = false;
  uint8_t minEl = 20;          // nur Überflüge, die mindestens so hoch steigen (Grad)
  bool visibleOnly = true;     // nur mit bloßem Auge sichtbare
  bool announce = true;        // ab 6 h vorher „ISS 21:43“ zwischendurch zeigen
  bool beep = false;           // 10 min vorher und beim Aufgang kurz piepen
};

constexpr int MAXP = 8;
constexpr double HORIZON = 10.0;  // ab 10° über dem Horizont zählt der Überflug

inline Config cfg;
inline SemaphoreHandle_t lock = nullptr;
inline Sat::Sgp4 model;
inline bool haveModel = false;
inline char tle1[72], tle2[72];
inline int64_t tleAt = 0;                 // Unix-Zeit des Abrufs
inline Sat::Pass passes[MAXP];
inline int passCount = 0;
inline int64_t computedAt = 0;
inline volatile bool recompute = true;
inline String error;
inline float liveAz = 0, liveEl = -90;
inline int64_t liveAt = 0;
inline double obsLat = 0, obsLon = 0;    // vom Flugmodus übernommen

inline void save() {
  auto &p = Settings::prefs;
  p.putBool("is_on", cfg.enabled); p.putUChar("is_min", cfg.minEl); p.putBool("is_vis", cfg.visibleOnly);
  p.putBool("is_ann", cfg.announce); p.putBool("is_beep", cfg.beep);
}

inline bool setTle(const char *a, const char *b, int64_t at, bool store) {
  Sat::Tle t;
  if (!Sat::parseTle(a, b, t)) return false;
  Sat::Sgp4 m;
  if (!m.init(t)) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  model = m; haveModel = true; tleAt = at;
  strncpy(tle1, a, 71); tle1[71] = 0; strncpy(tle2, b, 71); tle2[71] = 0;
  xSemaphoreGive(lock);
  if (store) { Settings::prefs.putString("is_l1", a); Settings::prefs.putString("is_l2", b); Settings::prefs.putLong64("is_at", at); }
  recompute = true;
  return true;
}

inline void begin() {
  lock = xSemaphoreCreateMutex();
  auto &p = Settings::prefs;
  cfg.enabled = p.getBool("is_on", false);
  cfg.minEl = constrain(p.getUChar("is_min", 20), 10, 80);
  cfg.visibleOnly = p.getBool("is_vis", true);
  cfg.announce = p.getBool("is_ann", true);
  cfg.beep = p.getBool("is_beep", false);
  String a = p.getString("is_l1", ""), b = p.getString("is_l2", "");
  if (a.length() >= 69 && b.length() >= 69) setTle(a.c_str(), b.c_str(), p.getLong64("is_at", 0), false);
}

inline void setConfig(const Config &c) { cfg = c; save(); recompute = true; }

// ---- Bahndaten holen ------------------------------------------------------------------
struct Txt { String s; };
inline void txtSink(const uint8_t *d, size_t n, void *u) { ((Txt *)u)->s.concat((const char *)d, n); }

inline int64_t nextTry = 0;

inline bool downloadTle() {
  static Fetch::Conn conn;
  Txt body; String err;
  int code = Fetch::get(conn, "celestrak.org", "/NORAD/elements/gp.php?CATNR=25544&FORMAT=TLE", txtSink, &body, 4096, err, false);
  conn.close();
  if (code != 200) { error = err.length() ? err : String("Bahndaten: HTTP ") + code; return false; }
  // Zeilen suchen, die mit "1 25544" und "2 25544" beginnen
  int i1 = body.s.indexOf("1 25544"), i2 = body.s.indexOf("2 25544");
  if (i1 < 0 || i2 < 0) { error = "Bahndaten unvollständig"; return false; }
  String a = body.s.substring(i1, i1 + 69), b = body.s.substring(i2, i2 + 69);
  if (!setTle(a.c_str(), b.c_str(), (int64_t)time(nullptr), true)) { error = "Bahndaten fehlerhaft (Prüfsumme)"; return false; }
  error = "";
  return true;
}

inline void yieldNow() { vTaskDelay(1); }

// Vom Hintergrund-Task alle 250 ms aufgerufen
inline void service(double lat, double lon) {
  if (!cfg.enabled || !Net::timeValid()) return;
  if (lat != obsLat || lon != obsLon) { obsLat = lat; obsLon = lon; recompute = true; }
  int64_t now = (int64_t)time(nullptr);
  bool online = WiFi.status() == WL_CONNECTED;

  // 1) Bahndaten: einmal am Tag (nach Fehler 30 min Pause), nicht in den ersten 40 s nach dem Start
  if (online && millis() > 40000 && now >= nextTry && (!haveModel || now - tleAt > 24 * 3600)) {
    Diag::flight(Diag::ST_ISS_TLE);
    if (!downloadTle()) nextTry = now + 1800; else nextTry = now + 3600;
    Diag::flight(Diag::ST_IDLE);
  }
  if (!haveModel) return;

  // 2) Überflüge neu berechnen: stündlich oder wenn sich etwas geändert hat
  if (recompute || now - computedAt > 3600) {
    recompute = false;
    Sat::Observer o; o.set(obsLat, obsLon, 50);
    Sat::Sgp4 m;
    xSemaphoreTake(lock, portMAX_DELAY); m = model; xSemaphoreGive(lock);
    static Sat::Pass fresh[MAXP];
    Diag::flight(Diag::ST_ISS_CALC);
    int n = Sat::findPasses(m, o, now, 48, HORIZON, cfg.minEl, cfg.visibleOnly, fresh, MAXP, yieldNow);
    Diag::flight(Diag::ST_IDLE);
    xSemaphoreTake(lock, portMAX_DELAY);
    memcpy(passes, fresh, sizeof(Sat::Pass) * n); passCount = n; computedAt = now;
    xSemaphoreGive(lock);
  }

  // 3) Während eines Überflugs jede Sekunde die aktuelle Richtung berechnen
  Sat::Pass p;
  bool have = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = 0; i < passCount; i++) if (passes[i].end >= now) { p = passes[i]; have = true; break; }
  Sat::Sgp4 m = model;
  xSemaphoreGive(lock);
  if (have && now >= p.start - 5 && now <= p.end + 5 && now != liveAt) {
    Sat::Observer o; o.set(obsLat, obsLon, 50);
    double az, el;
    if (Sat::elevation(m, o, (double)now, az, el)) { liveAz = (float)az; liveEl = (float)el; liveAt = now; }
  }
}

// Nächster (oder laufender) Überflug, thread-sicher
inline bool next(Sat::Pass &out, int64_t now) {
  if (!lock) return false;
  bool ok = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = 0; i < passCount; i++) if (passes[i].end >= now) { out = passes[i]; ok = true; break; }
  xSemaphoreGive(lock);
  return ok;
}

inline int list(Sat::Pass *out, int maxOut) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int n = passCount < maxOut ? passCount : maxOut;
  memcpy(out, passes, sizeof(Sat::Pass) * n);
  xSemaphoreGive(lock);
  return n;
}

}  // namespace Iss
