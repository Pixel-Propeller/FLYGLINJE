#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <math.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Settings.h"
#include "Net.h"
#include "Fetch.h"
#include "FlightParse.h"
#include "Badges.h"

// ---------------------------------------------------------------------------
// Einen bestimmten Flug verfolgen – weltweit, nicht nur im Radius. Die Uhr sucht
// das Rufzeichen bei adsb.lol (/v2/callsign), holt die Route (routeset) und
// rechnet aus Entfernung zum Ziel und Tempo die Ankunft aus. Landet der Flug,
// gibt es eine Meldung mit Ton. Läuft im Hintergrund-Task, höchstens 24 Stunden.
// ---------------------------------------------------------------------------
namespace Tracker {

using FlightParse::Plane;

enum State : uint8_t { OFF, SEARCH, AIR, LANDED, GIVEUP };
inline const char *STATE_TEXT[] = {"aus", "wird gesucht", "in der Luft", "gelandet", "nicht gefunden"};

inline SemaphoreHandle_t lock = nullptr;
inline String call;                       // z. B. EWG9AT
inline volatile uint8_t state = OFF;
inline Plane plane;                       // letzte Position
inline bool havePlane = false;
inline char from[5] = "", to[5] = "";
inline float destLat = 0, destLon = 0;
inline bool haveDest = false;
inline float destKm = -1;                 // Entfernung zum Ziel
inline int etaMin = -1;
inline uint32_t startedAt = 0;            // Unix-Zeit
inline uint32_t seenAt = 0, routeAt = 0, nextAt = 0;   // millis
inline uint32_t landedAt = 0;             // Unix-Zeit
inline volatile uint32_t landedVersion = 0;
inline String error;
inline Fetch::Conn conn;

inline void save() {
  Settings::prefs.putString("trk_cs", state == SEARCH || state == AIR ? call : String(""));
  Settings::prefs.putUInt("trk_at", startedAt);
}
inline void begin() {
  lock = xSemaphoreCreateMutex();
  String c = Settings::prefs.getString("trk_cs", "");
  uint32_t at = Settings::prefs.getUInt("trk_at", 0);
  if (c.length()) { call = c; startedAt = at; state = SEARCH; nextAt = 0; }   // nach Neustart weiter verfolgen
}

inline String cleanCall(String c) {
  c.trim(); c.toUpperCase();
  String o;
  for (size_t i = 0; i < c.length() && o.length() < 8; i++) { char ch = c[i]; if (isalnum((unsigned char)ch)) o += ch; }
  return o;
}
inline void start(const String &c) {
  xSemaphoreTake(lock, portMAX_DELAY);
  call = cleanCall(c); havePlane = false; haveDest = false; from[0] = to[0] = 0; destKm = -1; etaMin = -1; error = "";
  startedAt = Net::timeValid() ? (uint32_t)time(nullptr) : 0; landedAt = 0;
  state = call.length() >= 3 ? SEARCH : OFF; nextAt = 0; routeAt = 0;
  xSemaphoreGive(lock);
  save();
}
inline void stop() { xSemaphoreTake(lock, portMAX_DELAY); state = OFF; xSemaphoreGive(lock); save(); }
inline bool running() { return state == SEARCH || state == AIR; }

struct Txt { String s; };
inline void sink(const uint8_t *d, size_t n, void *u) { ((Txt *)u)->s.concat((const char *)d, n); }

struct RouteCtx { float lat, lon; bool ok; char from[5], to[5]; float dLat, dLon; };
inline void routeCb(const FlightParse::RouteInfo &r, void *u) {
  RouteCtx &x = *(RouteCtx *)u;
  if (r.airports < 2) return;
  int leg = FlightParse::bestLeg(r, x.lat, x.lon);
  if (leg < 0) leg = 0;
  strcpy(x.from, r.code[leg]); strcpy(x.to, r.code[leg + 1]);
  x.dLat = r.lat[leg + 1]; x.dLon = r.lon[leg + 1]; x.ok = true;
}

// Vom Hintergrund-Task (Flugmodus) aufgerufen
inline void service(double homeLat, double homeLon) {
  if (!running() || WiFi.status() != WL_CONNECTED || millis() < 45000) return;
  if (nextAt && (int32_t)(millis() - nextAt) < 0) return;
  // nach 24 Stunden aufgeben
  if (startedAt && Net::timeValid() && (uint32_t)time(nullptr) - startedAt > 86400) {
    xSemaphoreTake(lock, portMAX_DELAY); state = GIVEUP; xSemaphoreGive(lock); save(); return;
  }
  String path = "/v2/callsign/" + call;
  Txt body; String err;
  int code = Fetch::get(conn, "api.adsb.lol", path.c_str(), sink, &body, 16384, err, false);
  conn.close();
  if (code != 200) {
    xSemaphoreTake(lock, portMAX_DELAY); error = code == 429 ? String("zu viele Anfragen – kurze Pause") : err.length() ? err : "HTTP " + String(code); xSemaphoreGive(lock);
    nextAt = millis() + (code == 429 ? 300000UL : 120000UL);
    return;
  }
  // erstes Flugzeug in "ac":[ … ]
  Plane p; bool found = false;
  int a = body.s.indexOf("\"ac\":[");
  if (a >= 0) {
    char *buf = (char *)body.s.c_str();
    char *o = strchr(buf + a, '{');
    char *e = o ? FlightParse::blockEnd(o) : nullptr;
    if (o && e) { char sv = e[1]; e[1] = 0; found = FlightParse::parsePlane(o, homeLat, homeLon, p); e[1] = sv; }
  }
  uint8_t st = state;
  if (!found) {
    // verschwunden: kurz vor dem Ziel zählt das als Landung (Empfang endet oft im Anflug)
    if (st == AIR && haveDest && destKm >= 0 && destKm < 40 && millis() - seenAt > 4UL * 60000UL) {
      xSemaphoreTake(lock, portMAX_DELAY); state = LANDED; landedAt = Net::timeValid() ? time(nullptr) : 0; landedVersion++; xSemaphoreGive(lock);
      Badges::earn(Badges::LANDED); save(); return;
    }
    xSemaphoreTake(lock, portMAX_DELAY); error = st == AIR ? "gerade kein Empfang" : ""; xSemaphoreGive(lock);
    nextAt = millis() + 120000UL;
    return;
  }
  // Route (Start/Ziel mit Koordinaten) höchstens alle 30 min neu holen
  RouteCtx rc{p.lat, p.lon, false, "", "", 0, 0};
  if (!haveDest || millis() - routeAt > 30UL * 60000UL) {
    char req[128];
    snprintf(req, sizeof(req), "{\"planes\":[{\"callsign\":\"%s\",\"lat\":%.4f,\"lng\":%.4f}]}", call.c_str(), p.lat, p.lon);
    Txt rb; String e2;
    int c2 = Fetch::get(conn, "api.adsb.lol", "/api/0/routeset", sink, &rb, 16384, e2, false, req);
    conn.close();
    if (c2 == 200) FlightParse::parseRouteSet((char *)rb.s.c_str(), routeCb, &rc);
    routeAt = millis();
  }
  xSemaphoreTake(lock, portMAX_DELAY);
  plane = p; havePlane = true; seenAt = millis(); error = "";
  if (rc.ok) { strcpy(from, rc.from); strcpy(to, rc.to); destLat = rc.dLat; destLon = rc.dLon; haveDest = true; }
  if (haveDest) {
    destKm = (float)FlightParse::distanceKm(p.lat, p.lon, destLat, destLon);
    int gs = p.gsKmh > 120 ? p.gsKmh : 120;
    etaMin = (int)lroundf(destKm / gs * 60.0f) + (destKm > 60 ? 8 : 2);   // + Anflug
  }
  bool landed = st == AIR && (p.ground || (haveDest && destKm < 15 && p.altFt < 1500 && p.gsKmh < 260));
  if (landed) { state = LANDED; landedAt = Net::timeValid() ? time(nullptr) : 0; landedVersion++; }
  else if (!p.ground) state = AIR;
  xSemaphoreGive(lock);
  if (landed) { Badges::earn(Badges::LANDED); save(); return; }
  nextAt = millis() + (etaMin >= 0 && etaMin < 30 ? 30000UL : 60000UL);
}

// Laufschrift für die Uhr, z. B. „✈ EWG9AT > PMI IN 23 MIN“
inline String ticker() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String t;
  if (state == AIR) {
    t = "\x06 " + call;
    if (to[0]) t += String(" > ") + to;
    if (etaMin >= 0) t += etaMin <= 1 ? String(" LANDET GLEICH") : " IN " + String(etaMin) + " MIN";
    else if (havePlane) t += " " + String((plane.altFt + 50) / 100 * 100) + "FT";
  } else if (state == SEARCH) t = "\x06 " + call + " NOCH NICHT IN DER LUFT";
  xSemaphoreGive(lock);
  return t;
}

inline String json() {
  xSemaphoreTake(lock, portMAX_DELAY);
  String j = "{\"call\":\"" + call + "\",\"state\":" + String(state) + ",\"stateText\":\"" + STATE_TEXT[state] + "\",\"from\":\"" + from +
             "\",\"to\":\"" + to + "\",\"started\":" + String(startedAt) + ",\"landedAt\":" + String(landedAt);
  if (havePlane) {
    char b[48];
    snprintf(b, sizeof(b), ",\"lat\":%.4f,\"lon\":%.4f", plane.lat, plane.lon); j += b;
    j += ",\"hex\":\"" + String(plane.hex) + "\",\"type\":\"" + String(plane.type) + "\",\"reg\":\"" + String(plane.reg) + "\",\"alt\":" +
         String(plane.ground ? 0 : plane.altFt) + ",\"gs\":" + String(plane.gsKmh) + ",\"ago\":" + String((millis() - seenAt) / 1000);
  }
  if (destKm >= 0) { char b[24]; snprintf(b, sizeof(b), ",\"destKm\":%.0f", destKm); j += b; }
  j += ",\"eta\":" + String(etaMin);
  String e = error; e.replace("\"", "'");
  j += ",\"error\":\"" + e + "\"}";
  xSemaphoreGive(lock);
  return j;
}

}  // namespace Tracker
