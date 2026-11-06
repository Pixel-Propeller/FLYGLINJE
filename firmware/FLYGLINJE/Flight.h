#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Config.h"
#include "Settings.h"
#include "Net.h"
#include "FlightParse.h"
#include "Fetch.h"
#include "Stats.h"

// ---------------------------------------------------------------------------
// Flugmodus: holt Flüge im Umkreis von adsb.lol (Ersatz: airplanes.live),
// ergänzt die Route über adsb.lol (routeset, nur wenn sie zur Position passt) und stellt sie der Anzeige bereit.
// Die Abfragen laufen in einem eigenen Task, damit Uhr und Webinterface
// währenddessen flüssig bleiben.
// ---------------------------------------------------------------------------

namespace Flight {

using FlightParse::Plane;

// Seiten auf der Uhr (Bits in Config::pages)
// P_ROUTE ist seit 0.7.2 nur noch der Start („AB DUS“), das Ziel hat eine eigene Seite P_TO
// („AN PMI“). P_TO steht hinten, damit gespeicherte Einstellungen gültig bleiben.
enum Page : uint8_t { P_CALL, P_ROUTE, P_ALT, P_DIST, P_TYPE, P_DIR, P_SPEED, P_TO, PAGE_COUNT };
inline const char *PAGE_NAMES[PAGE_COUNT] = {"Flugnummer", "Start", "Flughöhe", "Entfernung", "Flugzeugtyp", "Richtung am Himmel", "Geschwindigkeit", "Ziel"};
// Reihenfolge auf der Uhr
constexpr Page PAGE_ORDER[PAGE_COUNT] = {P_CALL, P_ROUTE, P_TO, P_ALT, P_DIST, P_TYPE, P_DIR, P_SPEED};
constexpr uint8_t ROUTE_BITS = (1 << P_ROUTE) | (1 << P_TO);

struct Config {
  bool enabled = false;
  double lat = 51.6580, lon = 6.6172;    // Wesel
  uint8_t radiusKm = 10;                  // 1..50
  uint16_t maxAltFt = 0;                  // 0 = alle
  uint8_t poll = 0;                       // 0 adaptiv, 1 = 15 s, 2 = 60 s, 3 = sparsam
  uint8_t pages = 0xBF;                   // Flugnummer, Start, Ziel, Höhe, Entfernung, Typ, Richtung
  uint8_t maxPlanes = 4;                  // so viele Flüge nacheinander zeigen
  bool schedule = false;
  uint16_t fromMin = 7 * 60, toMin = 22 * 60 + 30;
  bool hideGround = true;
  uint8_t clockGapS = 10;                 // Uhr zwischen zwei Flügen (Sekunden)
  bool bar = true;                        // Punkte in der untersten Zeile (Flugzeuge / Seiten)
};

constexpr int MAX = 20;

inline Config cfg;
inline Plane planes[MAX];
inline int count = 0;
inline uint32_t updatedAt = 0;          // millis() der letzten erfolgreichen Abfrage
inline String error;
inline String source;
inline SemaphoreHandle_t lock = nullptr;
inline volatile bool refreshNow = false;
inline uint32_t version = 0;             // zählt bei jeder neuen Liste hoch
inline volatile bool paused = false;     // Anzeige nachts aus: nichts abfragen
inline uint32_t polls = 0, failures = 0;
inline TaskHandle_t taskHandle = nullptr;

// Fehlertext und Quelle sicher kopieren (werden im Hintergrund-Task geändert)
inline void statusText(String &err, String &src) {
  xSemaphoreTake(lock, portMAX_DELAY); err = error; src = source; xSemaphoreGive(lock);
}
// Zusatzarbeit im selben Hintergrund-Task (ISS-Überflüge), bekommt den Standort
inline void (*extraService)(double lat, double lon) = nullptr;

inline uint32_t stackFree() { return taskHandle ? uxTaskGetStackHighWaterMark(taskHandle) : 0; }

// ---- Einstellungen -----------------------------------------------------------
inline void save() {
  auto &p = Settings::prefs;
  p.putBool("fl_on", cfg.enabled);
  p.putDouble("fl_lat", cfg.lat);
  p.putDouble("fl_lon", cfg.lon);
  p.putUChar("fl_rad", cfg.radiusKm);
  p.putUShort("fl_alt", cfg.maxAltFt);
  p.putUChar("fl_poll", cfg.poll);
  p.putUChar("fl_pages", cfg.pages);
  p.putUChar("fl_max", cfg.maxPlanes);
  p.putBool("fl_sched", cfg.schedule);
  p.putUShort("fl_from", cfg.fromMin);
  p.putUShort("fl_to", cfg.toMin);
  p.putBool("fl_gnd", cfg.hideGround);
  p.putUChar("fl_gap", cfg.clockGapS);
  p.putBool("fl_bar", cfg.bar);
}
inline void load() {
  auto &p = Settings::prefs;
  cfg.enabled = p.getBool("fl_on", false);
  cfg.lat = p.getDouble("fl_lat", 51.6580);
  cfg.lon = p.getDouble("fl_lon", 6.6172);
  cfg.radiusKm = constrain(p.getUChar("fl_rad", 10), 1, 50);
  cfg.maxAltFt = p.getUShort("fl_alt", 0);
  cfg.poll = min<uint8_t>(p.getUChar("fl_poll", 0), 3);
  cfg.pages = p.getUChar("fl_pages", 0xBF);
  // vor 0.7.2 war „Route“ eine Seite: wer sie an hatte, bekommt Start und Ziel
  if (!p.getBool("fl_pv2", false)) { if (cfg.pages & (1 << P_ROUTE)) cfg.pages |= 1 << P_TO; p.putBool("fl_pv2", true); p.putUChar("fl_pages", cfg.pages); }
  cfg.maxPlanes = constrain(p.getUChar("fl_max", 4), 1, 8);
  cfg.schedule = p.getBool("fl_sched", false);
  cfg.fromMin = p.getUShort("fl_from", 7 * 60);
  cfg.toMin = p.getUShort("fl_to", 22 * 60 + 30);
  cfg.hideGround = p.getBool("fl_gnd", true);
  cfg.clockGapS = constrain(p.getUChar("fl_gap", 10), 3, 60);
  cfg.bar = p.getBool("fl_bar", true);
}

// Ist der Flugmodus gerade aktiv (an + im Zeitplan)?
inline bool activeNow() {
  if (!cfg.enabled) return false;
  if (!cfg.schedule || !Net::timeValid()) return true;
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  int m = t.tm_hour * 60 + t.tm_min;
  if (cfg.fromMin <= cfg.toMin) return m >= cfg.fromMin && m < cfg.toMin;
  return m >= cfg.fromMin || m < cfg.toMin;   // über Mitternacht
}

// ---- Routen (adsb.lol routeset) ------------------------------------------------------
// Gleiche Quelle wie die Karte auf adsb.lol: Route nur, wenn sie zur Position passt.
// Ergebnisse werden gemerkt: gefundene 24 h, „keine Route“ 2 h.
constexpr int ROUTE_SLOTS = 64;
struct RouteEntry { char call[9]; char from[5]; char to[5]; uint32_t at; uint32_t ttl; };
inline RouteEntry routes[ROUTE_SLOTS];
inline int routeNext = 0;

inline RouteEntry *findRoute(const char *call) {
  for (auto &r : routes) if (r.call[0] && strcmp(r.call, call) == 0) {
    if (millis() - r.at < r.ttl) return &r;
    r.call[0] = 0; return nullptr;
  }
  return nullptr;
}

inline void rememberRoute(const char *call, const char *from, const char *to) {
  RouteEntry *e = nullptr;
  for (auto &r : routes) if (r.call[0] && strcmp(r.call, call) == 0) e = &r;
  // Sammelalbum: Flughäfen einer neu gefundenen Route
  if (!e || strcmp(e->from, from) || strcmp(e->to, to)) { Album::airport(from); Album::airport(to); }
  if (!e) { e = &routes[routeNext]; routeNext = (routeNext + 1) % ROUTE_SLOTS; }
  strncpy(e->call, call, sizeof(e->call) - 1); e->call[sizeof(e->call) - 1] = 0;
  strncpy(e->from, from, 4); e->from[4] = 0; strncpy(e->to, to, 4); e->to[4] = 0;
  e->at = millis();
  e->ttl = (from[0] || to[0]) ? 24UL * 3600UL * 1000UL : 30UL * 60UL * 1000UL;   // „unbekannt“ nur 30 min merken
}

// ---- Besondere Flugzeuge ---------------------------------------------------------------
// prio 3 = Notfall (blinkt, Signalton), 2 = Rettungsflug, 1 = sehenswert, 0 = normal
struct Special { uint8_t prio; const char *text; };
inline Special special(const Plane &p) {
  using namespace FlightParse;
  if (p.squawk == 7500 || p.emerg == EMERG_UNLAWFUL) return {3, "ENTFÜHRUNG"};
  if (p.squawk == 7600 || p.emerg == EMERG_NORDO) return {3, "FUNKAUSFALL"};
  if (p.emerg == EMERG_MINFUEL) return {3, "SPRIT KNAPP"};
  if (p.squawk == 7700 || p.emerg == EMERG_GENERAL || p.emerg == EMERG_DOWNED) return {3, "NOTFALL"};
  if (p.emerg == EMERG_LIFEGUARD || strncmp(p.call, "CHX", 3) == 0) return {2, "RETTUNGSFLUG"};
  if (p.dbFlags & 1) return {1, "MILITÄR"};
  const char *t = p.type;
  if (!strcmp(t, "A388")) return {1, "A380"};
  if (!strcmp(t, "B744") || !strcmp(t, "B748") || !strcmp(t, "B74S") || !strcmp(t, "B742")) return {1, "JUMBO"};
  if (!strcmp(t, "BLCF")) return {1, "DREAMLIFTER"};
  if (!strcmp(t, "A3ST") || !strcmp(t, "A337")) return {1, "BELUGA"};
  if (!strcmp(t, "A124") || !strcmp(t, "A225")) return {1, "ANTONOV"};
  if (!strcmp(t, "CONC")) return {1, "CONCORDE"};
  if (!strcmp(p.cat, "A7")) return {1, "HUBSCHRAUBER"};
  if (!strcmp(p.cat, "B2")) return {1, "BALLON"};
  if (!strcmp(p.cat, "B1")) return {1, "SEGELFLUG"};
  return {0, ""};
}
// Notfälle und Rettungsflüge nach vorne, sonst bleibt die Sortierung nach Entfernung
inline void sortSpecialFirst(Plane *list, int n) {
  for (int i = 1; i < n; i++) {
    uint8_t pr = special(list[i]).prio;
    if (pr < 2) continue;
    int j = i;
    while (j > 0 && special(list[j - 1]).prio < pr) j--;
    if (j == i) continue;
    Plane tmp = list[i];
    memmove(&list[j + 1], &list[j], sizeof(Plane) * (i - j));
    list[j] = tmp;
  }
}

inline const char *statLabel(const Plane &p, uint8_t &prio) { Special s = special(p); prio = s.prio; return s.text; }

// ---- „Über dir“: Flug fast senkrecht über dem Standort (Höhenwinkel ab 60°) ------------
inline volatile uint32_t overVersion = 0;
inline char overCall[9] = "";
inline char overSeen[8][7];
inline int overNext = 0;
inline void checkOverhead(const Plane *list, int n) {
  for (int i = 0; i < n; i++) {
    const Plane &p = list[i];
    if (p.ground || p.altFt < 300 || p.elev < 60 || !p.hex[0]) continue;
    bool seen = false;
    for (auto &h : overSeen) if (strcmp(h, p.hex) == 0) seen = true;
    if (seen) continue;
    strncpy(overSeen[overNext], p.hex, 6); overSeen[overNext][6] = 0; overNext = (overNext + 1) % 8;
    xSemaphoreTake(lock, portMAX_DELAY); strncpy(overCall, p.call, 8); overCall[8] = 0; overVersion++; xSemaphoreGive(lock);
    Badges::earn(Badges::OVERHEAD);
    break;
  }
}

// ---- Abruf ---------------------------------------------------------------------------
inline Fetch::Conn listConn;     // Flugliste
inline Fetch::Conn routeConn;    // Routen

// ---- Schonender Abruf: Pause je Quelle nach Fehlern -----------------------------------
// Nach einem Fehler wartet die Uhr bei dieser Quelle, jedes weitere Mal doppelt so lange
// (30 s … 30 min). „Zu viele Anfragen“ (HTTP 429) beginnt bei 2 min; nennt der Server
// eine Wartezeit (Retry-After), gilt mindestens diese.
enum Src : uint8_t { S_LOL, S_LIVE, S_ROUTE, S_DB, S_COUNT };
inline const char *SRC_NAME[S_COUNT] = {"adsb.lol", "airplanes.live", "adsb.lol Routen", "adsbdb"};
struct Backoff { uint32_t until = 0; uint8_t level = 0; int lastCode = 0; };
inline Backoff backoff[S_COUNT];
inline bool srcReady(Src s) { return backoff[s].level == 0 || (int32_t)(millis() - backoff[s].until) >= 0; }
inline void srcOk(Src s) { backoff[s].level = 0; backoff[s].until = 0; }

// letzte Fehler fürs Webinterface (neueste zuerst)
constexpr int LOG_N = 8;
struct LogEntry { time_t at; uint32_t ms; char text[72]; };
inline LogEntry errLog[LOG_N];
inline int logCount = 0;
inline void logError(const String &t) {
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = LOG_N - 1; i > 0; i--) errLog[i] = errLog[i - 1];
  errLog[0].at = Net::timeValid() ? time(nullptr) : 0; errLog[0].ms = millis();
  strncpy(errLog[0].text, t.c_str(), sizeof(errLog[0].text) - 1); errLog[0].text[sizeof(errLog[0].text) - 1] = 0;
  if (logCount < LOG_N) logCount++;
  xSemaphoreGive(lock);
}

inline void srcFail(Src s, int code, const String &err) {
  Backoff &b = backoff[s];
  if (b.level < 7) b.level++;
  uint32_t base = code == 429 ? 120000UL : 30000UL;
  uint32_t wait = base << (b.level - 1);
  if (wait > 1800000UL) wait = 1800000UL;
  if (Fetch::lastRetryAfter > 0 && (uint32_t)Fetch::lastRetryAfter * 1000UL > wait) wait = (uint32_t)Fetch::lastRetryAfter * 1000UL;
  b.until = millis() + wait; b.lastCode = code;
  String why = code == 429 ? String("zu viele Anfragen (429)") : code > 0 ? "HTTP " + String(code) : err;
  logError(String(SRC_NAME[s]) + ": " + why + " · Pause " + String((wait + 59999) / 60000) + " min");
}

struct BodyText { String s; };
inline void textSink(const uint8_t *d, size_t n, void *u) { ((BodyText *)u)->s.concat((const char *)d, n); }

inline String routeInfo;   // für das Webinterface: woher die Routen kamen / Fehler

struct RouteCtx { Plane *list; int n; int found; };
inline void routeCb(const FlightParse::RouteInfo &r, void *u) {
  RouteCtx &x = *(RouteCtx *)u;
  if (r.airports < 2) return;                      // adsb.lol kennt keine Route -> später adsbdb fragen
  for (int i = 0; i < x.n; i++) {
    Plane &p = x.list[i];
    if (p.routeChecked || strcmp(p.call, r.call) != 0) continue;
    int leg = FlightParse::bestLeg(r, p.lat, p.lon);
    if (leg < 0) leg = 0;
    strcpy(p.from, r.code[leg]); strcpy(p.to, r.code[leg + 1]);
    p.routeChecked = true;
    rememberRoute(p.call, p.from, p.to);
    x.found++;
  }
}

// Ersatzquelle: adsbdb.com (auch nur Start oder nur Ziel)
inline int adsbdbLookup(const char *call, char *from, char *to) {
  BodyText body; String err;
  char path[40]; snprintf(path, sizeof(path), "/v0/callsign/%s", call);
  int code = Fetch::get(routeConn, "api.adsbdb.com", path, textSink, &body, 8192, err);
  from[0] = to[0] = 0;
  if (code == 200) FlightParse::parseRoute(body.s.c_str(), from, to);
  return code;     // < 0 = Netzfehler, 404 = unbekannt
}

// Routen nachschlagen: 1) adsb.lol (eine Anfrage für alle), 2) adsbdb für den Rest (max. 3 je Runde)
// Rufzeichen, die adsb.lol nicht kennt: dort 30 min nicht erneut fragen (gleich zu adsbdb)
constexpr int MISS_SLOTS = 32;
struct Miss { char call[9]; uint32_t at; };
inline Miss lolMiss[MISS_SLOTS];
inline int missNext = 0;
inline bool lolMissed(const char *call) {
  for (auto &m : lolMiss) if (m.call[0] && strcmp(m.call, call) == 0) {
    if (millis() - m.at < 30UL * 60UL * 1000UL) return true;
    m.call[0] = 0;
  }
  return false;
}
inline void addLolMiss(const char *call) {
  if (lolMissed(call)) return;
  Miss &m = lolMiss[missNext]; missNext = (missNext + 1) % MISS_SLOTS;
  strncpy(m.call, call, sizeof(m.call) - 1); m.call[sizeof(m.call) - 1] = 0; m.at = millis();
}
inline uint32_t lastRouteSet = 0;

inline void lookupRoutes(Plane *list, int n) {
  if (n <= 0) return;
  int fromLol = 0, fromDb = 0, none = 0;
  String body = "{\"planes\":[";
  int k = 0;
  for (int i = 0; i < n; i++) {
    Plane &p = list[i];
    if (!p.call[0] || p.routeChecked || lolMissed(p.call)) continue;
    char b[96];
    snprintf(b, sizeof(b), "%s{\"callsign\":\"%s\",\"lat\":%.4f,\"lng\":%.4f}", k ? "," : "", p.call, p.lat, p.lon);
    body += b; k++;
  }
  body += "]}";
  String err; bool sentLol = false;
  // höchstens alle 15 s eine Routen-Sammelanfrage, und nur wenn adsb.lol nicht pausiert
  if (k && srcReady(S_ROUTE) && (lastRouteSet == 0 || millis() - lastRouteSet >= 15000)) {
    lastRouteSet = millis(); sentLol = true;
    BodyText resp;
    Diag::flight(Diag::ST_FL_ROUTE);
    int code = Fetch::get(routeConn, "api.adsb.lol", "/api/0/routeset", textSink, &resp, 32768, err, false, body.c_str());
    routeConn.close();
    if (code == 200) {
      srcOk(S_ROUTE);
      RouteCtx ctx{list, n, 0};
      FlightParse::parseRouteSet(resp.s.begin(), routeCb, &ctx);
      fromLol = ctx.found;
      for (int i = 0; i < n; i++) if (list[i].call[0] && !list[i].routeChecked) addLolMiss(list[i].call);
      err = "";
    } else {
      srcFail(S_ROUTE, code, err);
      if (!err.length()) err = "adsb.lol Routen: HTTP " + String(code);
    }
  }
  // Ersatz adsbdb: für Flüge, die adsb.lol nicht kannte – oder alle, solange adsb.lol-Routen
  // pausieren; höchstens 2 je Runde
  int asked = 0;
  bool lolDown = !srcReady(S_ROUTE);
  for (int i = 0; i < n && asked < 2 && srcReady(S_DB); i++) {
    Plane &p = list[i];
    if (!p.call[0] || p.routeChecked || strlen(p.call) < 4) continue;
    if (!lolDown && !lolMissed(p.call)) continue;
    asked++;
    char f[5], t[5];
    int code = adsbdbLookup(p.call, f, t);
    if (code < 0 || code == 429 || code >= 500) { srcFail(S_DB, code, "keine Verbindung"); break; }
    srcOk(S_DB);
    strcpy(p.from, f); strcpy(p.to, t);
    p.routeChecked = true;
    rememberRoute(p.call, f, t);
    if (f[0] || t[0]) fromDb++; else none++;
  }
  routeConn.close();
  if (!sentLol && !asked) return;
  String info = "adsb.lol " + String(fromLol) + " · adsbdb " + String(fromDb) + " · unbekannt " + String(none) +
              (err.length() ? " · " + err : String(""));
  xSemaphoreTake(lock, portMAX_DELAY); routeInfo = info; xSemaphoreGive(lock);
}
// Für das Webinterface: Pausen und letzte Fehler als JSON
inline String backoffJson() {
  String j = "[";
  bool first = true;
  for (int i = 0; i < S_COUNT; i++) {
    if (srcReady((Src)i)) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"src\":\"" + String(SRC_NAME[i]) + "\",\"s\":" + String((backoff[i].until - millis()) / 1000) + ",\"code\":" + String(backoff[i].lastCode) + "}";
  }
  return j + "]";
}
inline String logJson() {
  String j = "[";
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = 0; i < logCount; i++) {
    if (i) j += ",";
    char t[8] = "";
    if (errLog[i].at) { struct tm tm; localtime_r(&errLog[i].at, &tm); snprintf(t, sizeof(t), "%02d:%02d", tm.tm_hour, tm.tm_min); }
    String txt = errLog[i].text; txt.replace("\\", "/"); txt.replace("\"", "'");
    j += "{\"t\":\"" + String(t) + "\",\"ago\":" + String((millis() - errLog[i].ms) / 1000) + ",\"msg\":\"" + txt + "\"}";
  }
  xSemaphoreGive(lock);
  return j + "]";
}
inline String routeText() { xSemaphoreTake(lock, portMAX_DELAY); String s = routeInfo; xSemaphoreGive(lock); return s; }

// Sammelt beim Mitlesen die nächsten Flugzeuge, sortiert nach Entfernung
struct ListCtx { const Config *c; Plane *out; int maxOut; int n; };
inline FlightParse::Stream parser;       // groß, deshalb nicht auf dem Stack

inline void listSink(const uint8_t *d, size_t len, void *u) {
  Diag::flight(Diag::ST_FL_PARSE);
  ListCtx &x = *(ListCtx *)u;
  const Config &c = *x.c;
  for (size_t i = 0; i < len; i++) {
    if (!parser.feed((char)d[i])) continue;
    Plane p;
    if (!FlightParse::parsePlane(parser.buf, c.lat, c.lon, p)) continue;
    if (p.distKm > c.radiusKm) continue;
    if (c.hideGround && (p.ground || p.altFt < 100)) continue;
    if (c.maxAltFt && p.altFt > c.maxAltFt) continue;
    if (!p.call[0]) strncpy(p.call, p.reg[0] ? p.reg : p.hex, sizeof(p.call) - 1);
    int pos = x.n;
    while (pos > 0 && x.out[pos - 1].distKm > p.distKm) pos--;
    if (pos >= x.maxOut) continue;
    int end = x.n < x.maxOut ? x.n : x.maxOut - 1;
    for (int k = end; k > pos; k--) x.out[k] = x.out[k - 1];
    x.out[pos] = p;
    if (x.n < x.maxOut) x.n++;
    taskYIELD();                         // andere Aufgaben (WLAN) zwischendurch ranlassen
  }
}

// Liest die Flugliste von einer Quelle. Rückgabe: Anzahl oder -1 bei Fehler
inline int fetchFrom(const char *host, const Config &c, Plane *out, int maxOut, String &err, int &code) {
  char path[64];
  int nm = (int)ceil(c.radiusKm / 1.852);          // Radius in Seemeilen, aufgerundet – nicht mehr als nötig laden
  snprintf(path, sizeof(path), "/v2/point/%.4f/%.4f/%d", c.lat, c.lon, nm < 1 ? 1 : nm);
  parser.reset();
  ListCtx x{&c, out, maxOut, 0};
  // Verbindung danach schließen: hält sonst dauerhaft ~40 KB RAM belegt
  code = Fetch::get(listConn, host, path, listSink, &x, 600000, err, false);
  if (code != 200) return -1;
  return x.n;
}

// ---- Hintergrund-Task ----------------------------------------------------------------
inline void task(void *) {
  static Plane fresh[MAX];
  uint32_t nextAt = 0, lastPoll = 0;
  int emptyRuns = 0;
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(250));
    Config c;
    xSemaphoreTake(lock, portMAX_DELAY); c = cfg; xSemaphoreGive(lock);
    if (extraService) extraService(c.lat, c.lon);
    bool online = WiFi.status() == WL_CONNECTED;
    bool active = activeNow() && !paused && !Net::otaRunning;
    if (!active || !online) {
      if (!active) { xSemaphoreTake(lock, portMAX_DELAY); count = 0; xSemaphoreGive(lock); emptyRuns = 0; }
      if (listConn.open) listConn.close();       // Speicher freigeben
      continue;
    }
    // erst 40 s nach dem Start: eine frisch installierte Version wird nach 30 s bestätigt,
    // so kann ein Fehler hier nie ein Update zurückwerfen
    if (millis() < 40000) continue;
    if (!refreshNow && (int32_t)(millis() - nextAt) < 0) continue;
    if (lastPoll && millis() - lastPoll < 10000) continue;        // nie öfter als alle 10 s
    // Quelle wählen: adsb.lol, sonst airplanes.live – pausierte Quellen werden übersprungen
    bool lolOk = srcReady(S_LOL), liveOk = srcReady(S_LIVE);
    if (!lolOk && !liveOk) {
      uint32_t a = backoff[S_LOL].until - millis(), b = backoff[S_LIVE].until - millis();
      nextAt = millis() + (a < b ? a : b);
      xSemaphoreTake(lock, portMAX_DELAY); error = "Beide Quellen pausieren gerade (zu viele Anfragen oder Fehler)"; xSemaphoreGive(lock);
      refreshNow = false;
      continue;
    }
    refreshNow = false;
    polls++; lastPoll = millis();

    String err; String src; int code = 0; int n = -1;
    if (lolOk) {
      src = "adsb.lol";
      n = fetchFrom("api.adsb.lol", c, fresh, MAX, err, code);
      if (n < 0) srcFail(S_LOL, code, err); else srcOk(S_LOL);
    }
    if (n < 0 && srcReady(S_LIVE)) {
      String err2; int code2 = 0; src = "airplanes.live";
      n = fetchFrom("api.airplanes.live", c, fresh, MAX, err2, code2);
      if (n < 0) { srcFail(S_LIVE, code2, err2); err += (err.length() ? " · " : "") + err2; } else srcOk(S_LIVE);
    }

    if (n >= 0) {
      // Routen: erst aus dem Speicher, die übrigen (angezeigten) mit einer Anfrage nachschlagen
      for (int i = 0; i < n; i++) {
        RouteEntry *r = findRoute(fresh[i].call);
        if (r) { strcpy(fresh[i].from, r->from); strcpy(fresh[i].to, r->to); fresh[i].routeChecked = true; }
      }
      sortSpecialFirst(fresh, n);              // Notfälle zuerst (auch wenn sie weiter weg sind)
      if (c.pages & ROUTE_BITS) lookupRoutes(fresh, n < c.maxPlanes ? n : c.maxPlanes);
      xSemaphoreTake(lock, portMAX_DELAY);
      memcpy(planes, fresh, sizeof(Plane) * n);
      count = n; updatedAt = millis(); error = ""; source = src; version++;
      xSemaphoreGive(lock);
      Stats::count(fresh, n, statLabel);
      checkOverhead(fresh, n);
      emptyRuns = n > 0 ? 0 : emptyRuns + 1;
    } else {
      failures++;
      xSemaphoreTake(lock, portMAX_DELAY); error = err; xSemaphoreGive(lock);
    }
    // Abstand bis zur nächsten Abfrage: mit Flugzeugen öfter, ohne seltener
    uint32_t wait;
    switch (c.poll) {
      case 1: wait = 15000; break;
      case 2: wait = 60000; break;
      case 3: wait = n > 0 ? 30000 : 120000; break;                 // sparsam
      default: wait = n > 0 ? 15000 : (emptyRuns > 20 ? 90000 : 45000); break;
    }
    if (n < 0) wait = 30000;          // die Pause der Quelle regelt srcFail; die andere darf es nach 30 s versuchen
    listConn.close();
    Diag::flight(Diag::ST_IDLE);
    nextAt = millis() + wait;
  }
}

inline void begin() {
  load();
  lock = xSemaphoreCreateMutex();
  Fetch::initLock();
  // WICHTIG: auf Kern 1 (wie loop()). Auf Kern 0 überwacht ein Watchdog, dass die
  // Leerlauf-Aufgabe mindestens alle 5 s drankommt. Der TLS-Verbindungsaufbau rechnet
  // bei 80 MHz mehrere Sekunden am Stück -> Watchdog -> Neustart (Fehler bis 0.4.7).
  xTaskCreatePinnedToCore(task, "flight", 20480, nullptr, 1, &taskHandle, 1);
}

// Kopie der aktuellen Liste (thread-sicher)
inline int snapshot(Plane *out, int maxOut, uint32_t *ver = nullptr) {
  xSemaphoreTake(lock, portMAX_DELAY);
  int n = count < maxOut ? count : maxOut;
  memcpy(out, planes, sizeof(Plane) * n);
  if (ver) *ver = version;
  xSemaphoreGive(lock);
  return n;
}

// Aktuelle Route eines Flugs (die Anzeige hält eine Kopie der Liste; Routen kommen oft erst später)
inline bool routeFor(const char *hex, char *from, char *to, bool &checked) {
  bool found = false;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int i = 0; i < count; i++) if (strcmp(planes[i].hex, hex) == 0) {
    strcpy(from, planes[i].from); strcpy(to, planes[i].to); checked = planes[i].routeChecked; found = true; break;
  }
  xSemaphoreGive(lock);
  return found;
}

// Beispielflüge für Test › Besondere Flugzeuge
constexpr int DEMO_COUNT = 11;
inline const char *DEMO_NAMES[DEMO_COUNT] = {"Normaler Flug", "Notfall (7700)", "Funkausfall (7600)", "Entführung (7500)", "Treibstoff knapp",
                                             "Rettungsflug", "Militär", "A380", "Beluga", "Nur Start bekannt", "Route unbekannt"};
inline Plane demoPlane(int k) {
  Plane p; memset(&p, 0, sizeof(p));
  auto set = [&](const char *call, const char *type, const char *reg, const char *from, const char *to, int alt, int gs) {
    strcpy(p.call, call); strcpy(p.type, type); strcpy(p.reg, reg); strcpy(p.from, from); strcpy(p.to, to);
    p.altFt = alt; p.gsKmh = gs; p.routeChecked = true;
  };
  strcpy(p.hex, "test00"); strcpy(p.cat, "A3");
  p.distKm = 4.2f; p.bearing = 225; p.elev = 38; p.rateFpm = 0; p.squawk = 1000;
  switch (k) {
    default:
    case 0: set("EWG9AT", "A320", "D-AIZA", "DUS", "PMI", 12000, 610); break;
    case 1: set("DLH4CK", "A321", "D-AISQ", "FRA", "LHR", 9000, 520); p.squawk = 7700; p.rateFpm = -2200; break;
    case 2: set("RYR82NM", "B738", "EI-DCL", "NRN", "STN", 24000, 760); p.squawk = 7600; break;
    case 3: set("KLM1858", "B737", "PH-BXA", "AMS", "VIE", 33000, 820); p.squawk = 7500; break;
    case 4: set("TUI6KD", "B38M", "D-AMAX", "PMI", "DUS", 6000, 430); p.emerg = FlightParse::EMERG_MINFUEL; p.squawk = 7700; break;
    case 5: set("CHX9", "EC35", "D-HDRI", "", "", 1500, 220); strcpy(p.cat, "A7"); p.emerg = FlightParse::EMERG_LIFEGUARD; p.distKm = 2.1f; p.elev = 12; p.routeChecked = true; break;
    case 6: set("GAF618", "A400", "54+03", "", "", 18000, 590); p.dbFlags = 1; strcpy(p.cat, "A5"); break;
    case 7: set("UAE55", "A388", "A6-EUA", "DUS", "DXB", 36000, 900); strcpy(p.cat, "A5"); break;
    case 8: set("BGA114", "A337", "F-GXLG", "XFW", "TLS", 28000, 780); strcpy(p.cat, "A5"); break;
    case 9: set("BCS3KE", "B752", "D-ALEJ", "CGN", "", 15000, 640); break;
    case 10: set("N104FX", "C25B", "N104FX", "", "", 8000, 480); strcpy(p.cat, "A2"); break;
  }
  return p;
}

inline void setConfig(const Config &c) {
  xSemaphoreTake(lock, portMAX_DELAY); cfg = c; xSemaphoreGive(lock);
  save();
  refreshNow = true;
}

// ---- Texte für die Seiten auf der Uhr ------------------------------------------------
inline String pageText(const Plane &p, Page page) {
  char b[16];
  switch (page) {
    case P_CALL: return p.call;
    // Start und Ziel getrennt (die Anzeige setzt links ein Abflug- bzw. Landesymbol davor):
    // „--“ nur für das, was wirklich unbekannt ist, „...“ solange die Route noch gesucht wird
    case P_ROUTE: return p.from[0] ? String(p.from) : String(p.routeChecked ? "--" : "...");
    case P_TO:    return p.to[0] ? String(p.to) : String(p.routeChecked ? "--" : "...");
    case P_ALT:
      if (p.ground) return "BODEN";
      snprintf(b, sizeof(b), "%ldFT", (long)((p.altFt + 50) / 100 * 100)); return b;
    case P_DIST:
      if (p.distKm < 10) { snprintf(b, sizeof(b), "%d,%dKM", (int)p.distKm, (int)(p.distKm * 10) % 10); }
      else snprintf(b, sizeof(b), "%dKM", (int)lroundf(p.distKm));
      return b;
    case P_TYPE: return p.type[0] ? String(p.type) : String("");
    case P_DIR: snprintf(b, sizeof(b), "%s %d\x04", FlightParse::compass(p.bearing), p.elev); return b;
    case P_SPEED: if (!p.gsKmh) return ""; snprintf(b, sizeof(b), "%dKMH", p.gsKmh); return b;
    default: return "";
  }
}

}  // namespace Flight
