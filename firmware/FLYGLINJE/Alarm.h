#pragma once
#include <Arduino.h>
#include <time.h>
#include "Config.h"
#include "Settings.h"
#include "Net.h"

// ---------------------------------------------------------------------------
// Zwei Wecker mit Wochentagen, Schlummern, Weckspruch und Klingelmustern
// ---------------------------------------------------------------------------
namespace Alarm {

constexpr int COUNT = 2;
// Wochentage als Bits: Bit 0 = Sonntag … Bit 6 = Samstag (wie tm_wday)
constexpr uint8_t DAYS_ALL = 0x7F, DAYS_WORK = 0x3E, DAYS_WEEKEND = 0x41, DAYS_ONCE = 0x00;
constexpr uint32_t RING_MAX_MS = 10UL * 60UL * 1000UL;   // klingelt höchstens 10 Minuten

struct Entry { bool on; uint8_t hour; uint8_t minute; uint8_t days; bool snooze = true; };   // snooze = Schlummern erlaubt

inline Entry alarms[COUNT];
inline bool master = true;          // "Wecker/Ton"-Taste schaltet alle Wecker an/aus
inline uint8_t snoozeMin = 9;
inline uint8_t melody = 0;          // 0 klassisch, 1 sanft, 2 lauter werdend
inline String text;                 // Weckspruch, läuft beim Klingeln über die Anzeige

enum State { IDLE, RINGING, SNOOZING };
inline State state = IDLE;
inline int active = -1;             // welcher Wecker klingelt/schlummert
inline uint32_t ringStart = 0, snoozeUntil = 0, stepAt = 0;
inline int step = 0;
inline int lastMinuteKey = -1;
inline bool preview = false;        // Probealarm
inline bool justStarted = false;    // App zeigt dann den Weckspruch

// ---- Speichern ----------------------------------------------------------------
inline uint32_t pack(const Entry &e) {
  // Bit 19 = Schlummern AUS (so bleibt es bei gespeicherten Weckern aus 0.4.x automatisch an)
  return (e.on ? 1u : 0u) | (uint32_t)(e.hour & 31) << 1 | (uint32_t)(e.minute & 63) << 6 | (uint32_t)(e.days & 0x7F) << 12 |
         (e.snooze ? 0u : 1u << 19);
}
inline Entry unpack(uint32_t v) {
  Entry e; e.on = v & 1; e.hour = (v >> 1) & 31; e.minute = (v >> 6) & 63; e.days = (v >> 12) & 0x7F; e.snooze = !(v & (1u << 19));
  if (e.hour > 23) e.hour = 7;
  if (e.minute > 59) e.minute = 0;
  return e;
}
inline void save() {
  auto &p = Settings::prefs;
  for (int i = 0; i < COUNT; i++) p.putUInt(i == 0 ? "al0" : "al1", pack(alarms[i]));
  p.putBool("almaster", master);
  p.putUChar("alsnooze", snoozeMin);
  p.putUChar("almelody", melody);
  p.putString("altext", text);
}
inline void begin() {
  auto &p = Settings::prefs;
  alarms[0] = unpack(p.getUInt("al0", pack({false, 7, 0, DAYS_WORK})));
  alarms[1] = unpack(p.getUInt("al1", pack({false, 9, 0, DAYS_WEEKEND})));
  master = p.getBool("almaster", true);
  snoozeMin = p.getUChar("alsnooze", 9);
  melody = p.getUChar("almelody", 0);
  text = p.getString("altext", "GUTEN MORGEN!");
}

// ---- Hilfen -----------------------------------------------------------------------
inline const char *daysLabel(uint8_t d) {
  switch (d) {
    case DAYS_ALL: return "TÄGL";
    case DAYS_WORK: return "MO-FR";
    case DAYS_WEEKEND: return "SA-SO";
    case DAYS_ONCE: return "EINMAL";
    default: return "EIGENE";
  }
}

// Minuten bis zum nächsten Klingeln dieses Weckers (oder -1)
inline int minutesUntil(const Entry &e) {
  if (!e.on || !Net::timeValid()) return -1;
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  int nowMin = t.tm_hour * 60 + t.tm_min;
  int target = e.hour * 60 + e.minute;
  for (int d = 0; d < 8; d++) {
    int wday = (t.tm_wday + d) % 7;
    int diff = d * 1440 + target - nowMin;
    if (diff <= 0) continue;
    if (e.days == DAYS_ONCE || (e.days & (1 << wday))) return diff;
  }
  return -1;
}

// Text für den nächsten Wecker, z. B. "MO 07:00" (leer = keiner)
inline String nextText() {
  if (!master) return "";
  int best = -1, bestIdx = -1;
  for (int i = 0; i < COUNT; i++) {
    int m = minutesUntil(alarms[i]);
    if (m >= 0 && (best < 0 || m < best)) { best = m; bestIdx = i; }
  }
  if (bestIdx < 0) return "";
  static const char *WD[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
  time_t at = time(nullptr) + (time_t)best * 60;
  struct tm t; localtime_r(&at, &t);
  char b[16];
  snprintf(b, sizeof(b), "%s %02d:%02d", WD[t.tm_wday], alarms[bestIdx].hour, alarms[bestIdx].minute);
  return b;
}

// ---- Klingeln -------------------------------------------------------------------------
inline void startRing(int idx, bool isPreview = false) {
  active = idx; state = RINGING; ringStart = millis(); step = 0; stepAt = 0;
  preview = isPreview; justStarted = true;
}

inline void stop() {
  noTone(Pin::BUZZER);
  if (state != IDLE && !preview && active >= 0 && alarms[active].days == DAYS_ONCE) {
    alarms[active].on = false; save();   // "Einmal"-Wecker schaltet sich nach dem Klingeln ab
  }
  state = IDLE; active = -1; preview = false;
}

// Darf der gerade klingelnde Wecker schlummern?
inline bool canSnooze() { return state == RINGING && !preview && active >= 0 && alarms[active].snooze; }

// Schlummern; ist es für diesen Wecker abgeschaltet, wird er stattdessen beendet
inline bool snooze() {
  if (state != RINGING) return false;
  noTone(Pin::BUZZER);
  if (!canSnooze()) { stop(); return false; }
  state = SNOOZING;
  snoozeUntil = millis() + (uint32_t)snoozeMin * 60000UL;
  return true;
}

// Klingelmuster: Liste aus (Frequenz, Dauer ms), 0 Hz = Pause
struct Note { uint16_t hz; uint16_t ms; };
constexpr Note CLASSIC[] = {{2700, 90}, {0, 60}, {2700, 90}, {0, 60}, {2700, 90}, {0, 60}, {2700, 90}, {0, 460}};
constexpr Note SOFT[]    = {{1568, 160}, {0, 40}, {2093, 160}, {0, 40}, {2637, 220}, {0, 1380}};
constexpr Note RISE[]    = {{2400, 60}, {0, 940}};   // wird mit der Zeit dichter

inline void ringStep() {
  uint32_t now = millis();
  if ((int32_t)(now - stepAt) < 0) return;
  const Note *pat; int len;
  uint32_t elapsed = now - ringStart;
  if (melody == 1) { pat = SOFT; len = sizeof(SOFT) / sizeof(Note); }
  else if (melody == 2) { pat = RISE; len = sizeof(RISE) / sizeof(Note); }
  else { pat = CLASSIC; len = sizeof(CLASSIC) / sizeof(Note); }
  Note n = pat[step % len];
  uint16_t ms = n.ms;
  if (melody == 2 && n.hz == 0) {
    // lauter werdend: Pause wird pro Minute kürzer (bis 60 ms)
    uint32_t minutes = elapsed / 60000;
    ms = (uint16_t)max<int32_t>(60, 940 - (int32_t)minutes * 180);
  }
  if (n.hz) tone(Pin::BUZZER, n.hz, n.ms); else noTone(Pin::BUZZER);
  stepAt = now + ms;
  step++;
}

inline void update() {
  uint32_t now = millis();

  if (state == RINGING) {
    if (now - ringStart > (preview ? 6000UL : RING_MAX_MS)) { stop(); return; }
    ringStep();
    return;
  }
  if (state == SNOOZING) {
    if ((int32_t)(now - snoozeUntil) >= 0) { int a = active; startRing(a); }
    return;
  }

  // einmal pro Minute prüfen, ob ein Wecker dran ist
  if (!Net::timeValid() || !master) return;
  time_t t0 = time(nullptr); struct tm t; localtime_r(&t0, &t);
  int key = t.tm_yday * 1440 + t.tm_hour * 60 + t.tm_min;
  if (key == lastMinuteKey) return;
  bool first = lastMinuteKey < 0;
  lastMinuteKey = key;
  if (first && t.tm_sec > 5) return;        // nicht nachträglich klingeln, wenn die Uhr mitten in der Minute startet
  for (int i = 0; i < COUNT; i++) {
    const Entry &e = alarms[i];
    if (!e.on || e.hour != t.tm_hour || e.minute != t.tm_min) continue;
    if (e.days == DAYS_ONCE || (e.days & (1 << t.tm_wday))) { startRing(i); break; }
  }
}

}  // namespace Alarm
