#pragma once
#include <Arduino.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Settings.h"
#include "Net.h"
#include "FlightParse.h"
#include "Badges.h"
#include "Album.h"

// ---------------------------------------------------------------------------
// Flugstatistik: wie viele verschiedene Flugzeuge heute und insgesamt im Radius
// waren, Rekorde (höchster, schnellster, nächster Flug) und die besonderen
// Flugzeuge der letzten 7 Tage. Wird nach jedem Abruf im Hintergrund-Task
// gezählt; gespeichert wird sparsam (Flash schonen).
// ---------------------------------------------------------------------------
namespace Stats {

using FlightParse::Plane;

constexpr int SEEN_MAX = 700;           // verschiedene Flugzeuge pro Tag (ICAO-Adresse)
inline uint32_t seen[SEEN_MAX];
inline int seenCount = 0;
inline int dayKey = -1;                  // Jahr*400 + Tag im Jahr
inline uint32_t total = 0, totalSince = 0;   // Unix-Zeit des ersten Zählens
inline uint32_t yesterday = 0;

struct Rec { int32_t v = 0; char call[9] = ""; char type[5] = ""; uint32_t at = 0; };
inline Rec recHigh, recFast, recNear;    // höchster (ft), schnellster (km/h), nächster (m)

struct Rare { uint32_t hex = 0; char call[9] = ""; char type[5] = ""; char what[14] = ""; uint32_t at = 0; };
constexpr int RARE_MAX = 16;
inline Rare rare[RARE_MAX];
inline int rareCount = 0;

inline SemaphoreHandle_t lock = nullptr;
inline bool dirtyRec = false, dirtyRare = false;
inline uint32_t lastSave = 0;

struct Blob { uint32_t ver, total, since, yday; Rec high, fast, nearest; };

inline void load() {
  lock = xSemaphoreCreateMutex();
  auto &p = Settings::prefs;
  Blob b;
  if (p.getBytesLength("st_main") == sizeof(Blob)) {
    p.getBytes("st_main", &b, sizeof(b));
    total = b.total; totalSince = b.since; yesterday = b.yday; recHigh = b.high; recFast = b.fast; recNear = b.nearest;
  }
  size_t n = p.getBytesLength("st_rare");
  if (n && n % sizeof(Rare) == 0 && n <= sizeof(rare)) { p.getBytes("st_rare", rare, n); rareCount = n / sizeof(Rare); }
}
inline void saveNow() {
  Blob b{1, total, totalSince, yesterday, recHigh, recFast, recNear};
  Settings::prefs.putBytes("st_main", &b, sizeof(b));
  if (dirtyRare) Settings::prefs.putBytes("st_rare", rare, sizeof(Rare) * rareCount);
  dirtyRec = dirtyRare = false; lastSave = millis();
}

inline uint32_t hexNum(const char *h) { return (uint32_t)strtoul(h, nullptr, 16); }

inline void setRec(Rec &r, int32_t v, const Plane &p) {
  r.v = v; strncpy(r.call, p.call, 8); r.call[8] = 0; strncpy(r.type, p.type, 4); r.type[4] = 0;
  r.at = Net::timeValid() ? (uint32_t)time(nullptr) : 0; dirtyRec = true;
}

// nach jedem erfolgreichen Abruf; label = Text der Besonderheit (leer = normal)
typedef const char *(*LabelFn)(const Plane &p, uint8_t &prio);
inline void count(const Plane *list, int n, LabelFn label) {
  if (!lock || !Net::timeValid()) return;
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  int key = t.tm_year * 400 + t.tm_yday;
  xSemaphoreTake(lock, portMAX_DELAY);
  if (dayKey != key) {
    if (dayKey >= 0) { yesterday = seenCount; dirtyRec = true; }
    dayKey = key; seenCount = 0;
  }
  if (!totalSince) totalSince = (uint32_t)now;
  for (int i = 0; i < n; i++) {
    const Plane &p = list[i];
    uint32_t h = hexNum(p.hex);
    if (!h) continue;
    bool known = false;
    for (int k = 0; k < seenCount; k++) if (seen[k] == h) { known = true; break; }
    if (!known) {
      if (seenCount < SEEN_MAX) seen[seenCount++] = h;
      total++;
      Badges::earn(Badges::FIRST);
      Album::sight(p);                       // Sammelalbum: einmal je Flugzeug und Tag
      if (seenCount >= 100) Badges::earn(Badges::DAY100);
      if (total >= 1000) Badges::earn(Badges::TOTAL1000);
      if (total >= 10000) Badges::earn(Badges::TOTAL10000);
    }
    if (p.ground) continue;
    if (p.altFt > 43000 && p.altFt < 60000) Badges::earn(Badges::HIGHFLY);
    if (p.distKm < 1.0f) Badges::earn(Badges::LOWFLY);
    if (p.altFt > recHigh.v && p.altFt < 60000) setRec(recHigh, p.altFt, p);
    if (p.gsKmh > recFast.v && p.gsKmh < 1400) setRec(recFast, p.gsKmh, p);
    int32_t m = (int32_t)lroundf(p.distKm * 1000);
    if (!recNear.at || m < recNear.v) setRec(recNear, m, p);
    // Besondere: je Flugzeug nur einmal in 7 Tagen merken
    uint8_t prio = 0;
    const char *what = label(p, prio);
    if (prio == 3) Badges::earn(Badges::EMERGENCY);
    else if (!strcmp(what, "A380")) Badges::earn(Badges::A380);
    else if (!strcmp(what, "JUMBO")) Badges::earn(Badges::JUMBO);
    else if (!strcmp(what, "BELUGA")) Badges::earn(Badges::BELUGA);
    else if (!strcmp(what, "MILITÄR")) Badges::earn(Badges::MILITARY);
    else if (!strcmp(what, "HUBSCHRAUBER")) Badges::earn(Badges::HELI);
    else if (!strcmp(what, "RETTUNGSFLUG")) Badges::earn(Badges::RESCUE);
    if (prio) {
      bool have = false;
      for (int k = 0; k < rareCount; k++) if (rare[k].hex == h && (uint32_t)now - rare[k].at < 7 * 86400) { have = true; break; }
      if (!have) {
        if (rareCount == RARE_MAX) rareCount--;
        memmove(&rare[1], &rare[0], sizeof(Rare) * rareCount);
        Rare &r = rare[0]; r = Rare();
        r.hex = h; strncpy(r.call, p.call, 8); strncpy(r.type, p.type, 4); strncpy(r.what, what, 13); r.at = (uint32_t)now;
        rareCount++; dirtyRare = true;
      }
    }
  }
  // ältere als 7 Tage fallen raus
  while (rareCount && (uint32_t)now - rare[rareCount - 1].at > 7 * 86400) { rareCount--; dirtyRare = true; }
  bool save = (dirtyRare || dirtyRec || seenCount) && (millis() - lastSave > 30UL * 60UL * 1000UL || dirtyRare);
  if (save) saveNow();
  xSemaphoreGive(lock);
}

inline int today() { return seenCount; }

inline String esc(const char *s) { String r = s; r.replace("\"", "'"); r.replace("\\", "/"); return r; }
inline String recJson(const char *name, const Rec &r) {
  return String("\"") + name + "\":{\"v\":" + r.v + ",\"call\":\"" + esc(r.call) + "\",\"type\":\"" + esc(r.type) + "\",\"at\":" + r.at + "}";
}
inline String json() {
  if (!lock) return "{}";
  xSemaphoreTake(lock, portMAX_DELAY);
  String j = "{\"today\":" + String(seenCount) + ",\"yesterday\":" + String(yesterday) + ",\"total\":" + String(total) +
             ",\"since\":" + String(totalSince) + "," + recJson("high", recHigh) + "," + recJson("fast", recFast) + "," +
             recJson("near", recNear) + ",\"rare\":[";
  for (int i = 0; i < rareCount; i++) {
    if (i) j += ",";
    char hx[8]; snprintf(hx, sizeof(hx), "%06lx", (unsigned long)rare[i].hex);
    j += "{\"hex\":\"" + String(hx) + "\",\"call\":\"" + esc(rare[i].call) + "\",\"type\":\"" + esc(rare[i].type) + "\",\"what\":\"" + esc(rare[i].what) + "\",\"at\":" + rare[i].at + "}";
  }
  xSemaphoreGive(lock);
  return j + "]}";
}

inline void reset() {
  xSemaphoreTake(lock, portMAX_DELAY);
  seenCount = 0; total = 0; totalSince = 0; yesterday = 0; recHigh = Rec(); recFast = Rec(); recNear = Rec(); rareCount = 0;
  dirtyRare = true; saveNow();
  xSemaphoreGive(lock);
}

}  // namespace Stats
