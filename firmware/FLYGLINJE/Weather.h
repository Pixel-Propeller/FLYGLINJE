#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Settings.h"
#include "Fetch.h"
#include "Diag.h"
#include "Badges.h"

// ---------------------------------------------------------------------------
// Wettermodus: Temperatur und Regenwahrscheinlichkeit am Standort des Flugradars.
// Quelle: Open-Meteo (kostenlos, ohne Schlüssel, Daten CC BY 4.0). Abruf alle
// 15 Minuten im Hintergrund-Task; die Uhr zeigt die Werte in einstellbarem Abstand
// kurz zwischen der Uhrzeit (Thermometer + „12°C“, Schirm + „40%“).
// Regen = höchste Wahrscheinlichkeit von jetzt bis in 3 Stunden.
// ---------------------------------------------------------------------------
namespace Weather {

inline bool enabled = false;
inline uint8_t everyMin = 1;            // alle x Minuten zeigen (1, 2, 5, 10)
inline SemaphoreHandle_t lock = nullptr;
inline float temp = NAN;                // °C
inline int rain = -1;                   // % (0..100), -1 = unbekannt
inline int code = -1;                   // WMO-Wettercode
inline uint32_t gotAt = 0;              // millis() des letzten erfolgreichen Abrufs
inline String error;
inline volatile bool refresh = true;
inline uint32_t nextAt = 0;
inline double lastLat = 999, lastLon = 999;

inline void load() {
  enabled = Settings::prefs.getBool("wx_on", false);
  everyMin = Settings::prefs.getUChar("wx_every", 1);
  if (everyMin < 1 || everyMin > 10) everyMin = 1;
}
inline void save() { Settings::prefs.putBool("wx_on", enabled); Settings::prefs.putUChar("wx_every", everyMin); }
inline void begin() { lock = xSemaphoreCreateMutex(); load(); }

// Daten frisch genug zum Anzeigen? (bis 2 h alt)
inline bool valid() { return gotAt && millis() - gotAt < 2UL * 3600UL * 1000UL && !isnan(temp); }

struct Txt { String s; };
inline void sink(const uint8_t *d, size_t n, void *u) { ((Txt *)u)->s.concat((const char *)d, n); }

// Zahl hinter "key": im Block ab from (nur bis zum Ende dieses Blocks)
inline bool numIn(const String &b, int from, const char *key, float &out) {
  int end = b.indexOf('}', from);
  String k = String("\"") + key + "\":";
  int i = b.indexOf(k, from);
  if (i < 0 || (end >= 0 && i > end)) return false;
  i += k.length();
  while (i < (int)b.length() && b[i] == ' ') i++;
  if (b.startsWith("null", i)) return false;
  out = atof(b.c_str() + i);
  return true;
}

inline bool fetch(double lat, double lon) {
  static Fetch::Conn conn;
  char path[220];
  snprintf(path, sizeof(path),
           "/v1/forecast?latitude=%.3f&longitude=%.3f&current=temperature_2m,precipitation_probability,weather_code"
           "&hourly=precipitation_probability&forecast_hours=3&timezone=auto", lat, lon);
  Txt body; String err;
  int c = Fetch::get(conn, "api.open-meteo.com", path, sink, &body, 6144, err, false);
  conn.close();
  if (c != 200) { xSemaphoreTake(lock, portMAX_DELAY); error = err.length() ? err : "HTTP " + String(c); xSemaphoreGive(lock); return false; }
  const String &b = body.s;
  int cur = b.indexOf("\"current\":{");
  float t = NAN, pp = -1, wc = -1;
  if (cur < 0 || !numIn(b, cur, "temperature_2m", t)) {
    xSemaphoreTake(lock, portMAX_DELAY); error = "Antwort unvollständig"; xSemaphoreGive(lock); return false;
  }
  numIn(b, cur, "precipitation_probability", pp);
  numIn(b, cur, "weather_code", wc);
  // stündliche Werte: [10,20,40] – das Maximum zählt
  int h = b.indexOf("\"hourly\":{");
  int a = h < 0 ? -1 : b.indexOf("\"precipitation_probability\":[", h);
  if (a >= 0) {
    a = b.indexOf('[', a) + 1;
    int e = b.indexOf(']', a);
    while (a > 0 && a < e) {
      if (!b.startsWith("null", a)) { float v = atof(b.c_str() + a); if (v > pp) pp = v; }
      int comma = b.indexOf(',', a);
      if (comma < 0 || comma > e) break;
      a = comma + 1;
    }
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  temp = t; rain = pp < 0 ? -1 : (int)lroundf(pp); code = (int)wc; gotAt = millis(); error = "";
  xSemaphoreGive(lock);
  if (t <= 0) Badges::earn(Badges::FROST);
  if (t >= 30) Badges::earn(Badges::HEAT);
  if (pp >= 100) Badges::earn(Badges::RAIN);
  return true;
}

// Vom Hintergrund-Task (Flugmodus) alle 250 ms aufgerufen
inline void service(double lat, double lon) {
  if (!enabled || WiFi.status() != WL_CONNECTED || millis() < 45000) return;
  if (fabs(lat - lastLat) > 0.001 || fabs(lon - lastLon) > 0.001) { lastLat = lat; lastLon = lon; refresh = true; }
  if (!refresh && (int32_t)(millis() - nextAt) < 0) return;
  refresh = false;
  Diag::flight(Diag::ST_IDLE);
  bool ok = fetch(lat, lon);
  nextAt = millis() + (ok ? 15UL : 5UL) * 60UL * 1000UL;   // alle 15 min, nach Fehler 5 min
}

// Für die Anzeige: Kopie der Werte
inline bool snapshot(float &t, int &r) {
  if (!lock) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ok = valid(); t = temp; r = rain;
  xSemaphoreGive(lock);
  return ok;
}

inline String json() {
  String j = "{\"enabled\":" + String(enabled ? "true" : "false") + ",\"every\":" + String(everyMin);
  xSemaphoreTake(lock, portMAX_DELAY);
  if (valid()) {
    char b[16]; snprintf(b, sizeof(b), "%.1f", temp);
    j += ",\"temp\":" + String(b) + ",\"rain\":" + String(rain) + ",\"code\":" + String(code) +
         ",\"age\":" + String((millis() - gotAt) / 1000);
  }
  String e = error; e.replace("\"", "'");
  j += ",\"error\":\"" + e + "\"";
  xSemaphoreGive(lock);
  return j + "}";
}

}  // namespace Weather
