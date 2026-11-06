#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Settings.h"
#include "Fetch.h"
#include "FlightParse.h"
#include "Badges.h"

// ---------------------------------------------------------------------------
// Unwetterwarnungen des Deutschen Wetterdienstes für den Standort des Radars,
// über Bright Sky (api.brightsky.dev/alerts, frei, ohne Schlüssel). Abruf alle
// 10 Minuten. Stufen wie beim DWD: 1 Wetterwarnung, 2 markant, 3 Unwetter, 4 extrem.
// ---------------------------------------------------------------------------
namespace Alerts {

inline bool enabled = true;
inline uint8_t minLevel = 2;              // erst ab „markantem Wetter“ melden
struct A { char event[40]; uint8_t level; char from[6]; char until[6]; char untilDay[3]; uint32_t id; };
constexpr int MAXA = 4;
inline A list[MAXA];
inline int count = 0;
inline String place, error;
inline uint32_t gotAt = 0, nextAt = 0;
inline volatile uint32_t version = 0;     // steigt, wenn eine neue Warnung dazukommt
inline volatile bool refresh = true;
inline uint32_t seenIds[12];
inline int seenNext = 0;
inline SemaphoreHandle_t lock = nullptr;
inline Fetch::Conn conn;

inline void load() {
  lock = xSemaphoreCreateMutex();
  enabled = Settings::prefs.getBool("uw_on", true);
  minLevel = constrain(Settings::prefs.getUChar("uw_min", 2), 1, 4);
}
inline void save() { Settings::prefs.putBool("uw_on", enabled); Settings::prefs.putUChar("uw_min", minLevel); }

inline uint8_t levelOf(const char *s) {
  if (!strcmp(s, "extreme")) return 4;
  if (!strcmp(s, "severe")) return 3;
  if (!strcmp(s, "moderate")) return 2;
  return 1;
}
inline uint32_t hash(const char *s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

struct Txt { String s; };
inline void sink(const uint8_t *d, size_t n, void *u) { ((Txt *)u)->s.concat((const char *)d, n); }

inline void fetch(double lat, double lon) {
  char path[96];
  snprintf(path, sizeof(path), "/alerts?lat=%.3f&lon=%.3f&tz=Europe/Berlin", lat, lon);
  Txt body; String err;
  int code = Fetch::get(conn, "api.brightsky.dev", path, sink, &body, 32768, err, false);
  conn.close();
  if (code != 200) {
    xSemaphoreTake(lock, portMAX_DELAY); error = err.length() ? err : "HTTP " + String(code); xSemaphoreGive(lock);
    nextAt = millis() + 5UL * 60000UL;
    return;
  }
  A fresh[MAXA]; int n = 0; bool isNew = false; uint8_t top = 0;
  char *buf = (char *)body.s.c_str();
  char *arr = strstr(buf, "\"alerts\"");
  char *p = arr ? strchr(arr, '[') : nullptr;
  char *end = p ? FlightParse::blockEnd(p) : nullptr;
  while (p && end && (p = strchr(p + 1, '{')) != nullptr && p < end) {
    char *e = FlightParse::blockEnd(p);
    if (!e) break;
    char sv = e[1]; e[1] = 0;
    char v[48], sev[16], aid[64];
    sev[0] = aid[0] = 0;
    FlightParse::get(p, "severity", sev, sizeof(sev));
    uint8_t lv = levelOf(sev);
    if (lv >= minLevel && n < MAXA) {
      A &a = fresh[n]; memset(&a, 0, sizeof(a)); a.level = lv;
      if (FlightParse::get(p, "event_de", v, sizeof(v))) { strncpy(a.event, v, sizeof(a.event) - 1); }
      if (FlightParse::get(p, "onset", v, sizeof(v)) && strlen(v) >= 16) { memcpy(a.from, v + 11, 5); }
      if (FlightParse::get(p, "expires", v, sizeof(v)) && strlen(v) >= 16) { memcpy(a.until, v + 11, 5); memcpy(a.untilDay, v + 8, 2); }
      FlightParse::get(p, "alert_id", aid, sizeof(aid));
      a.id = hash(aid[0] ? aid : a.event);
      bool seen = false;
      for (auto s : seenIds) if (s == a.id) seen = true;
      if (!seen) { seenIds[seenNext] = a.id; seenNext = (seenNext + 1) % 12; isNew = true; }
      if (lv > top) top = lv;
      n++;
    }
    e[1] = sv;
    p = e;
  }
  // Ort (Warnkreis) fürs Webinterface
  String pl;
  char *loc = strstr(buf, "\"location\"");
  if (loc) { char v[48]; if (FlightParse::get(loc, "name", v, sizeof(v))) pl = v; }
  xSemaphoreTake(lock, portMAX_DELAY);
  memcpy(list, fresh, sizeof(A) * n); count = n; gotAt = millis(); error = ""; place = pl;
  if (isNew) version++;
  xSemaphoreGive(lock);
  if (n) Badges::earn(Badges::STORM);
  nextAt = millis() + 10UL * 60000UL;
}

inline void service(double lat, double lon) {
  if (!enabled || WiFi.status() != WL_CONNECTED || millis() < 50000) return;
  if (!refresh && nextAt && (int32_t)(millis() - nextAt) < 0) return;
  refresh = false;
  fetch(lat, lon);
}

// Laufschrift zur stärksten Warnung, z. B. „! STURMBÖEN (STUFE 2) BIS 18:00“ – leer = keine
inline String ticker() {
  if (!lock) return "";
  xSemaphoreTake(lock, portMAX_DELAY);
  String t;
  int best = -1;
  for (int i = 0; i < count; i++) if (best < 0 || list[i].level > list[best].level) best = i;
  if (best >= 0 && millis() - gotAt < 3UL * 3600UL * 1000UL) {
    const A &a = list[best];
    t = String(a.level >= 3 ? "!! UNWETTER: " : "! WARNUNG: ") + a.event + " (STUFE " + a.level + ")";
    if (a.until[0]) t += String(" BIS ") + a.until;
    if (count > 1) t += "  +" + String(count - 1) + " WEITERE";
  }
  xSemaphoreGive(lock);
  return t;
}
inline uint8_t topLevel() {
  uint8_t t = 0;
  if (!lock) return 0;
  xSemaphoreTake(lock, portMAX_DELAY); for (int i = 0; i < count; i++) if (list[i].level > t) t = list[i].level; xSemaphoreGive(lock);
  return t;
}

inline String json() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String j = "{\"enabled\":" + String(enabled ? "true" : "false") + ",\"min\":" + String(minLevel) + ",\"place\":\"" + place + "\",\"list\":[";
  for (int i = 0; i < count; i++) {
    if (i) j += ",";
    String ev = list[i].event; ev.replace("\"", "'");
    j += "{\"event\":\"" + ev + "\",\"level\":" + String(list[i].level) + ",\"from\":\"" + list[i].from + "\",\"until\":\"" + list[i].until +
         "\",\"untilDay\":\"" + list[i].untilDay + "\"}";
  }
  String e = error; e.replace("\"", "'");
  j += "],\"age\":" + String(gotAt ? (millis() - gotAt) / 1000 : -1) + ",\"error\":\"" + e + "\"}";
  xSemaphoreGive(lock);
  return j;
}

}  // namespace Alerts
