#pragma once
#include <Arduino.h>
#include <time.h>
#include "Settings.h"
#include "Net.h"

// ---------------------------------------------------------------------------
// Kleine Extras (0.7.4):
//  * Besondere Tage: an einem Datum läuft ein eigener Text, vorher ein Countdown
//  * Geheime Nachricht: „Helligkeit“ 5 s halten
//  * Wetter nach dem Wecker, Meldung „Über dir“, Lichtwecker (nur Schalter hier)
//  * Küchentimer
// ---------------------------------------------------------------------------
namespace Extras {

inline bool wakeWeather = true;    // nach dem Wecker einmal das Wetter zeigen
inline bool overhead = true;       // Meldung, wenn ein Flug fast senkrecht über dem Standort ist
inline bool lightAlarm = false;    // Anzeige wird 10 min vor dem Wecker langsam heller
inline String secret;              // geheime Nachricht (leer = aus)
inline bool song = true;           // Geburtstagsständchen an besonderen Tagen
inline int songDay = -1;           // an welchem Tag (Jahr*400+Tag) es zuletzt lief

// ---- Besondere Tage -------------------------------------------------------------------
// gespeichert als Zeilen „TT.MM|Tage vorher|Name|Text“
struct Day { uint8_t d = 0, m = 0, before = 0; String name, text; };
constexpr int MAXD = 12;
inline Day days[MAXD];
inline int dayCount = 0;

inline String clean(String s, size_t max) {
  s.replace("|", "/"); s.replace("\n", " "); s.replace("\r", " "); s.trim();
  if (s.length() > max) s = s.substring(0, max);
  return s;
}

inline void parseDays(const String &raw) {
  dayCount = 0;
  int pos = 0;
  while (pos < (int)raw.length() && dayCount < MAXD) {
    int nl = raw.indexOf('\n', pos);
    String line = raw.substring(pos, nl < 0 ? raw.length() : nl);
    pos = nl < 0 ? raw.length() : nl + 1;
    int a = line.indexOf('|'), b = line.indexOf('|', a + 1), c = line.indexOf('|', b + 1);
    if (a < 0 || b < 0 || c < 0) continue;
    Day x;
    x.d = line.substring(0, 2).toInt(); x.m = line.substring(3, 5).toInt();
    if (x.d < 1 || x.d > 31 || x.m < 1 || x.m > 12) continue;
    x.before = constrain(line.substring(a + 1, b).toInt(), 0, 60);
    x.name = line.substring(b + 1, c); x.text = line.substring(c + 1);
    if (x.name.isEmpty() && x.text.isEmpty()) continue;
    days[dayCount++] = x;
  }
}
inline String daysRaw() {
  String r;
  char b[8];
  for (int i = 0; i < dayCount; i++) {
    snprintf(b, sizeof(b), "%02d.%02d", days[i].d, days[i].m);
    r += String(b) + "|" + days[i].before + "|" + days[i].name + "|" + days[i].text + "\n";
  }
  return r;
}

inline void load() {
  auto &p = Settings::prefs;
  wakeWeather = p.getBool("x_wwx", true);
  overhead = p.getBool("x_over", true);
  lightAlarm = p.getBool("x_light", false);
  secret = p.getString("x_secret", "");
  song = p.getBool("x_song", true);
  songDay = p.getInt("x_songd", -1);
  parseDays(p.getString("x_days", ""));
}
inline void save() {
  auto &p = Settings::prefs;
  p.putBool("x_wwx", wakeWeather); p.putBool("x_over", overhead); p.putBool("x_light", lightAlarm);
  p.putString("x_secret", secret); p.putString("x_days", daysRaw()); p.putBool("x_song", song);
}

// Tage bis zum nächsten Termin (0 = heute, -1 = keine Zeit)
inline int daysUntil(const Day &x) {
  if (!Net::timeValid()) return -1;
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  struct tm a = t; a.tm_hour = 12; a.tm_min = 0; a.tm_sec = 0;
  time_t today = mktime(&a);
  for (int y = 0; y < 5; y++) {               // 5 Jahre: auch der 29.02. findet sein Schaltjahr
    struct tm e = a; e.tm_mday = x.d; e.tm_mon = x.m - 1; e.tm_year = t.tm_year + y; e.tm_isdst = -1;
    time_t when = mktime(&e);
    if (e.tm_mday != x.d) continue;            // 29.02. in einem Nicht-Schaltjahr
    long diff = lround(difftime(when, today) / 86400.0);
    if (diff >= 0) return (int)diff;
  }
  return -1;
}

// Was gerade dran ist: heute ein Tag (Text mit Herzen) oder ein Countdown. Leer = nichts.
inline String dayMessage(bool *isToday = nullptr) {
  String count;
  int best = 999;
  for (int i = 0; i < dayCount; i++) {
    int n = daysUntil(days[i]);
    if (n < 0) continue;
    if (n == 0) {
      if (isToday) *isToday = true;
      const String &t = days[i].text.length() ? days[i].text : days[i].name;
      return "\x05 " + t + " \x05";
    }
    if (n <= days[i].before && n < best && days[i].name.length()) {
      best = n;
      count = n == 1 ? "MORGEN: " + days[i].name : "NOCH " + String(n) + " TAGE BIS " + days[i].name;
    }
  }
  if (isToday) *isToday = false;
  return count;
}

// ---- Küchentimer ------------------------------------------------------------------------
inline uint32_t timerEnd = 0;       // millis(); 0 = aus
inline bool timerRinging = false;
inline uint32_t timerRingStart = 0;
inline bool timerRunning() { return timerEnd != 0; }
inline int32_t timerLeftMs() { return timerEnd ? (int32_t)(timerEnd - millis()) : 0; }
inline void timerStart(uint32_t sec) {
  timerRinging = false;
  if (sec == 0) { timerEnd = 0; return; }
  if (sec > 99 * 60) sec = 99 * 60;
  timerEnd = millis() + sec * 1000UL;
  if (timerEnd == 0) timerEnd = 1;
}
inline void timerStop() { timerEnd = 0; timerRinging = false; }
// ± Minuten auf einen laufenden Timer (aus: + startet mit so vielen Minuten)
inline void timerAdd(int minutes) {
  if (!timerRunning()) { if (minutes > 0) timerStart(minutes * 60); return; }
  int32_t sec = (timerLeftMs() + 999) / 1000 + minutes * 60;
  timerStart(sec > 0 ? (uint32_t)sec : 0);
}
inline String timerText(int32_t ms) {
  if (ms < 0) ms = 0;
  uint32_t s = (ms + 999) / 1000;
  char b[8]; snprintf(b, sizeof(b), "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
  return b;
}

inline String json() {
  String j = "{\"wakeWeather\":" + String(wakeWeather ? "true" : "false") + ",\"overhead\":" + String(overhead ? "true" : "false") +
             ",\"lightAlarm\":" + String(lightAlarm ? "true" : "false") + ",\"secretSet\":" + String(secret.length() ? "true" : "false") +
             ",\"song\":" + String(song ? "true" : "false");
  String s = secret; s.replace("\\", "/"); s.replace("\"", "'");
  j += ",\"secret\":\"" + s + "\",\"days\":[";
  for (int i = 0; i < dayCount; i++) {
    if (i) j += ",";
    String n = days[i].name, t = days[i].text;
    n.replace("\\", "/"); n.replace("\"", "'"); t.replace("\\", "/"); t.replace("\"", "'");
    char b[8]; snprintf(b, sizeof(b), "%02d.%02d", days[i].d, days[i].m);
    j += "{\"date\":\"" + String(b) + "\",\"before\":" + days[i].before + ",\"name\":\"" + n + "\",\"text\":\"" + t + "\",\"in\":" + daysUntil(days[i]) + "}";
  }
  j += "],\"timer\":{\"running\":" + String(timerRunning() ? "true" : "false") + ",\"left\":" + String(timerRunning() ? (timerLeftMs() + 999) / 1000 : 0) +
       ",\"ringing\":" + String(timerRinging ? "true" : "false") + "}}";
  return j;
}

}  // namespace Extras
