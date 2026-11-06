#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Update.h>
#include <WiFi.h>
#include <lwip/sockets.h>
#include <ArduinoOTA.h>
#include "Config.h"
#include "Settings.h"
#include "Display.h"
#include "Inputs.h"
#include "Net.h"
#include "App.h"
#include "Alarm.h"
#include "Updater.h"
#include "Flight.h"
#include <sys/time.h>
#include "WebUI.h"
#include "Fetch.h"
#include "Diag.h"
#include "Iss.h"
#include "Extras.h"
#include "Stats.h"
#include "Badges.h"
#include "Tracker.h"
#include "Alerts.h"
#include "Album.h"
#include "nvs.h"
#include "Power.h"
#include "Profile.h"
#include "mbedtls/sha1.h"
#include "mbedtls/base64.h"

// ---------------------------------------------------------------------------
// Webinterface + JSON-Schnittstelle
// ---------------------------------------------------------------------------
namespace Web {

inline WebServer server(80);

// ---- Anmeldung -------------------------------------------------------------
struct Session { String token; uint32_t lastSeen = 0, created = 0; };
constexpr uint32_t SESSION_MS = 7UL * 24UL * 3600UL * 1000UL;   // Anmeldung gilt 7 Tage
constexpr int MAX_SESSIONS = 6;
inline Session sessions[MAX_SESSIONS];
inline int failedLogins = 0;
inline uint8_t lockLevel = 0;      // wächst mit jeder Sperre: 30 s, 1, 2, 4 … bis 60 min
inline uint32_t lockedUntil = 0;
inline uint32_t rebootAt = 0;
inline bool updateOk = false;
inline String updateError;

inline String cookieToken() {
  String c = server.header("Cookie");
  int i = c.indexOf("sid=");
  if (i < 0) return "";
  int e = c.indexOf(';', i);
  return c.substring(i + 4, e < 0 ? c.length() : e);
}

inline bool authed() {
  String t = cookieToken();
  if (t.length() != 32) return false;
  for (auto &s : sessions)
    if (s.token.length() && Settings::safeEquals(s.token, t)) {
      if (millis() - s.created > SESSION_MS) { s.token = ""; return false; }   // abgelaufen
      s.lastSeen = millis(); return true;
    }
  return false;
}

// ---- Nur aus dem eigenen Netz -------------------------------------------------------
// Antwortet nur Geräten mit privater Adresse (Heimnetz, Einrichtungs-WLAN). Zusätzlich muss
// der aufgerufene Name zur Uhr passen – so kann auch eine fremde Webseite im Browser nicht
// über einen Umweg (DNS-Rebinding) auf die Uhr zugreifen.
inline bool privateIp(const IPAddress &ip) {
  return ip[0] == 10 || (ip[0] == 172 && (ip[1] & 0xF0) == 16) || (ip[0] == 192 && ip[1] == 168) ||
         (ip[0] == 169 && ip[1] == 254) || ip[0] == 127;
}
inline bool hostOk() {
  if (Net::state == Net::SETUP_AP) return true;            // Captive Portal: beliebige Namen
  String h = server.hostHeader(); h.toLowerCase();
  int c = h.indexOf(':'); if (c >= 0) h = h.substring(0, c);
  if (h.endsWith(".")) h.remove(h.length() - 1);
  String host = Settings::d.hostname; host.toLowerCase();
  if (h.isEmpty() || h == host || h == WiFi.localIP().toString() || h == WiFi.softAPIP().toString()) return true;
  IPAddress ip;
  if (ip.fromString(h)) return privateIp(ip);                // direkt per IP-Adresse aus dem Heimnetz
  // Name der Uhr im Heimnetz, z. B. flyglinje.local oder flyglinje.fritz.box
  static const char *SUFFIX[] = {".local", ".fritz.box", ".lan", ".home", ".home.arpa", ".internal", ".box", ".speedport.ip"};
  for (auto sfx : SUFFIX) if (h == host + sfx || h == String("flyglinje") + sfx) return true;
  return false;
}
inline bool fromHome() {
  if (privateIp(server.client().remoteIP()) && hostOk()) return true;
  server.sendHeader("Cache-Control", "no-store");
  server.send(403, "text/plain; charset=utf-8", "Die FLYGLINJE antwortet nur im eigenen WLAN.");
  return false;
}

// Ersteinrichtung: solange noch kein Heimnetz gespeichert ist, darf man das WLAN ohne Anmeldung setzen
inline bool firstSetup() { return Settings::d.wifiSsid.isEmpty(); }

inline String newSession() {
  int slot = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].token.isEmpty()) { slot = i; break; }
    if (sessions[i].lastSeen < sessions[slot].lastSeen) slot = i;
  }
  sessions[slot].token = Settings::randomHex(16);
  sessions[slot].lastSeen = sessions[slot].created = millis();
  return sessions[slot].token;
}

inline void clearSessions() { for (auto &s : sessions) s.token = ""; }

// ---- Antworten -----------------------------------------------------------------
inline String esc(const String &s) {
  String o; o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o;
}
inline String q(const String &s) { return "\"" + esc(s) + "\""; }

// Schutz-Header für alle Antworten: kein Einbetten in fremde Seiten, keine
// fremden Skripte außer der Karte (unpkg/Leaflet), Kartenkacheln und Adresssuche.
inline void secHeaders() {
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("Referrer-Policy", "no-referrer");
  server.sendHeader("Content-Security-Policy",
    "default-src 'self'; script-src 'self' 'unsafe-inline' https://unpkg.com; style-src 'self' 'unsafe-inline' https://unpkg.com; "
    "img-src 'self' data: https://*.tile.openstreetmap.org https://tile.openstreetmap.org https://unpkg.com "
    "https://*.plnspttrs.net https://*.planespotters.net https://upload.wikimedia.org https://*.wikimedia.org; "
    "connect-src 'self' ws: https://nominatim.openstreetmap.org https://api.planespotters.net https://de.wikipedia.org https://en.wikipedia.org; frame-ancestors 'none'; base-uri 'none'; form-action 'self'");
}

inline void sendJson(const String &body, int code = 200) {
  secHeaders();
  server.sendHeader("Cache-Control", "no-store");
  server.send(code, "application/json", body);
}
inline void ok() { sendJson("{\"ok\":true}"); }
inline void fail(const String &msg, int code = 400) { sendJson("{\"ok\":false,\"error\":" + q(msg) + "}", code); }

inline bool requireAuth() {
  if (!fromHome()) return false;
  if (authed()) return true;
  fail("Bitte anmelden", 401);
  return false;
}

// ---- Spieler 2 (Pong zu zweit) ohne Anmeldung --------------------------------------------
// Startet jemand Pong zu zweit, wartet die Uhr auf Spieler 2. Auf der Anmeldeseite erscheint
// dann „Spieler 2“: ein Tipp gibt diesem Handy einen Gast-Zugang, der NUR die rechte Seite
// dieses einen Spiels steuern darf und mit dem Spiel endet.
inline String guestToken;
inline String cookieValue(const String &c, const char *name) {
  String k = String(name) + "=";
  int i = c.indexOf(k);
  if (i < 0) return "";
  int e = c.indexOf(';', i);
  return c.substring(i + k.length(), e < 0 ? c.length() : e);
}
inline bool guestValid(const String &cookie) {
  if (guestToken.isEmpty() || !((Game::kind == Game::PONG && Game::twoPlayer) || Game::kind == Game::QUARTETT)) return false;
  String t = cookieValue(cookie, "gid");
  return t.length() == 32 && Settings::safeEquals(guestToken, t);
}
// Admin oder Spieler 2; guest = true bei Spieler 2
inline bool requireGameAuth(bool &guest) {
  guest = false;
  if (!fromHome()) return false;
  if (authed()) return true;
  if (guestValid(server.header("Cookie"))) { guest = true; return true; }
  fail("Bitte anmelden", 401);
  return false;
}

inline String uptimeText() {
  uint32_t s = millis() / 1000;
  uint32_t d = s / 86400, h = (s % 86400) / 3600, m = (s % 3600) / 60;
  String t;
  if (d) t += String(d) + (d == 1 ? " Tag, " : " Tage, ");
  t += String(h) + " Std., " + String(m) + " Min.";
  return t;
}

// Warum ist der ESP zuletzt gestartet? Hilft bei der Fehlersuche ohne USB-Kabel.
inline String resetReasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON:  return "Strom eingeschaltet";
    case ESP_RST_SW:       return "Neustart (Update/Webinterface)";
    case ESP_RST_PANIC:    return "Absturz (Firmware-Fehler)";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "Hängengeblieben (Watchdog)";
    case ESP_RST_BROWNOUT: return "Unterspannung (Stromversorgung zu schwach)";
    case ESP_RST_EXT:      return "Reset-Taste";
    case ESP_RST_DEEPSLEEP:return "Aufgewacht";
    default:               return "unbekannt";
  }
}

// ---- Handler ---------------------------------------------------------------------
// Seite liegt gzip-komprimiert in der Firmware (16 statt 57 KB). Hat der Browser sie
// schon (gleiches ETag), antwortet die Uhr nur mit "304 – unverändert".
inline void handleRoot() {
  if (!fromHome()) return;
  secHeaders();
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("ETag", WebUI::ETAG);
  if (server.header("If-None-Match") == WebUI::ETAG) { server.send(304); return; }
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/html; charset=utf-8", (const char *)WebUI::PAGE_GZ, WebUI::PAGE_GZ_LEN);
}

// Flughafen-Namen fürs Album (statisch, ändert sich nur mit der Firmware)
inline void handleAirportsJs() {
  if (!fromHome()) return;
  secHeaders();
  server.sendHeader("Cache-Control", "no-cache");
  server.sendHeader("ETag", WebUI::AP_ETAG);
  if (server.header("If-None-Match") == WebUI::AP_ETAG) { server.send(304); return; }
  server.sendHeader("Content-Encoding", "gzip");
  server.send_P(200, "text/javascript; charset=utf-8", (const char *)WebUI::AP_GZ, WebUI::AP_GZ_LEN);
}

inline void handleState() {
  if (!fromHome()) return;
  String j = "{";
  j += "\"auth\":" + String(authed() ? "true" : "false");
  j += ",\"firstSetup\":" + String(firstSetup() ? "true" : "false");
  j += ",\"apMode\":" + String(Net::state == Net::SETUP_AP ? "true" : "false");
  j += ",\"defaultLogin\":" + String(Settings::d.defaultLogin ? "true" : "false");
  j += ",\"user\":" + q(authed() ? Settings::d.user : "");
  j += ",\"version\":" + q(FW_VERSION);
  j += ",\"model\":" + q(FW_MODEL);
  j += ",\"host\":" + q(Settings::d.hostname);
  j += "}";
  sendJson(j);
}

inline void handleLogin() {
  if (!fromHome()) return;
  int32_t left = (int32_t)(lockedUntil - millis());
  if (left > 0) {
    int s = (left + 999) / 1000;
    fail("Zu viele Fehlversuche. Bitte " + (s >= 90 ? String((s + 59) / 60) + " Minuten" : String(s) + " Sekunden") + " warten.", 429);
    return;
  }
  if (Settings::checkLogin(server.arg("user"), server.arg("pass"))) {
    failedLogins = 0; lockLevel = 0;
    String t = newSession();
    server.sendHeader("Set-Cookie", "sid=" + t + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=604800");
    sendJson("{\"ok\":true,\"defaultLogin\":" + String(Settings::d.defaultLogin ? "true" : "false") + "}");
  } else {
    delay(800);
    if (++failedLogins >= 5) {
      uint32_t ms = 30000UL << (lockLevel < 7 ? lockLevel : 7);
      if (ms > 3600000UL) ms = 3600000UL;
      lockedUntil = millis() + ms; failedLogins = 0;
      if (lockLevel < 8) lockLevel++;
    }
    fail("Benutzername oder Passwort falsch", 401);
  }
}

inline void handleLogout() {
  String t = cookieToken();
  for (auto &s : sessions) if (s.token == t) s.token = "";
  server.sendHeader("Set-Cookie", "sid=; Path=/; Max-Age=0");
  ok();
}

inline void handleAccount() {
  if (!requireAuth()) return;
  String user = server.arg("user"); user.trim();
  String cur = server.arg("cur"), neu = server.arg("new");
  if (user.isEmpty() || user.length() > 32) { fail("Benutzername fehlt oder ist zu lang"); return; }
  if (neu.length() < 8) { fail("Das neue Passwort braucht mindestens 8 Zeichen"); return; }
  if (!Settings::d.defaultLogin && !Settings::checkLogin(Settings::d.user, cur)) { fail("Aktuelles Passwort stimmt nicht"); return; }
  Settings::setPassword(user, neu, false);
  ArduinoOTA.setPasswordHash(Settings::d.otaHash.c_str());
  // alle anderen Sitzungen abmelden, diese behalten
  String keep = cookieToken();
  for (auto &s : sessions) if (s.token != keep) s.token = "";
  ok();
}

inline void handleStatus() {
  if (!requireAuth()) return;
  String time = "--:--", date = "";
  if (Net::timeValid()) {
    time_t now = ::time(nullptr); struct tm t; localtime_r(&now, &t);
    char b[24];
    strftime(b, sizeof(b), "%H:%M", &t); time = b;
    strftime(b, sizeof(b), "%d.%m.%Y", &t); date = b;
  }
  bool online = Net::state == Net::ONLINE || WiFi.status() == WL_CONNECTED;
  String j = "{";
  j += "\"time\":" + q(time) + ",\"date\":" + q(date);
  j += ",\"online\":" + String(online ? "true" : "false");
  j += ",\"ssid\":" + q(online ? WiFi.SSID() : "");
  j += ",\"rssi\":" + String(online ? WiFi.RSSI() : 0);
  j += ",\"ip\":" + q(Net::ip());
  j += ",\"host\":" + q(Settings::d.hostname);
  j += ",\"uptime\":" + q(uptimeText());
  j += ",\"heap\":" + String(ESP.getFreeHeap());
  j += ",\"reset\":" + q(Diag::lastText) + ",\"boots\":" + String(Settings::bootCount);
  j += ",\"rlog\":" + q(Diag::history()) + ",\"rollback\":" + q(Diag::rollbackText);
  {
    int espMa, ledMa, lit; Power::estimate(espMa, ledMa, lit);
    float mA = 0, volt = 0; bool meas = Power::measure(mA, volt);
    char b[40];
    j += ",\"pw\":{\"esp\":" + String(espMa) + ",\"led\":" + String(ledMa) + ",\"lit\":" + String(lit) + ",\"duty\":" + String(Power::duty()) + ",\"save\":" + String(Settings::d.powerSave ? 1 : 0) + ",\"ina\":" + String(meas ? "true" : "false");
    if (meas) { snprintf(b, sizeof(b), ",\"mA\":%.0f,\"V\":%.2f", mA, volt); j += b; }
    j += "}";
  }
  j += ",\"heapMin\":" + String(ESP.getMinFreeHeap()) + ",\"heapBlock\":" + String(ESP.getMaxAllocHeap());
  j += ",\"flStack\":" + String(Flight::stackFree()) + ",\"flFail\":" + String(Flight::failures);
  j += ",\"version\":" + q(FW_VERSION);
  j += ",\"brightness\":" + String(Settings::d.brightness);
  j += ",\"sound\":" + String(Settings::d.sound ? "true" : "false");
  j += ",\"rotate\":" + String(Settings::d.rotate180 ? "true" : "false");
  j += ",\"blink\":" + String(Settings::d.colonBlink ? "true" : "false");
  j += ",\"greeting\":" + q(Settings::d.greeting);
  j += ",\"tz\":" + q(Settings::d.tz);
  j += ",\"user\":" + q(Settings::d.user);
  j += ",\"defaultLogin\":" + String(Settings::d.defaultLogin ? "true" : "false");
  j += ",\"timeValid\":" + String(Net::timeValid() ? "true" : "false");
  j += ",\"alarmNext\":" + q(Alarm::nextText());
  j += ",\"alarmMaster\":" + String(Alarm::master ? "true" : "false");
  j += ",\"alarmState\":" + q(Alarm::state == Alarm::RINGING ? "ringing" : Alarm::state == Alarm::SNOOZING ? "snoozing" : "idle");
  j += ",\"alarmCanSnooze\":" + String(Alarm::canSnooze() ? "true" : "false");
  j += ",\"dots\":[" + String(Settings::d.dotRole[0]) + "," + String(Settings::d.dotRole[1]) + "," + String(Settings::d.dotRole[2]) + "," + String(Settings::d.dotRole[3]) + "]";
  j += ",\"nightNow\":" + String(Profile::active == 1 ? "true" : "false") + ",\"profile\":" + String(Profile::active) + ",\"profMode\":" + String(Profile::mode) + ",\"intro\":" + String(Settings::d.intro ? "true" : "false");
  j += ",\"flightOn\":" + String(Flight::cfg.enabled ? "true" : "false") + ",\"flightActive\":" + String(Flight::activeNow() ? "true" : "false") + ",\"flightCount\":" + String(Flight::count);
  j += ",\"sec\":{\"ap\":" + q("FLYGLINJE-" + Net::chipSuffix()) + ",\"apPass\":" + q(Settings::d.apPass) +
       ",\"ideOta\":" + String(Settings::d.ideOta ? "true" : "false") + "}";
  j += ",\"power\":{\"save\":" + String(Settings::d.powerSave ? "true" : "false") + ",\"mhz\":" + String(getCpuFrequencyMhz()) + "}";
  j += ",\"data\":{\"wire\":" + String(Fetch::bytesWire) + ",\"plain\":" + String(Fetch::bytesPlain) + ",\"polls\":" + String(Flight::polls) +
       ",\"tls\":" + String(Fetch::handshakes) + ",\"reused\":" + String(Fetch::reused) + "}";
  j += ",\"fw\":{\"url\":" + q(Settings::d.updateUrl) + ",\"auto\":" + String(Settings::d.autoUpdate ? "true" : "false") +
       ",\"latest\":" + q(Updater::latest) + ",\"available\":" + String(Updater::available ? "true" : "false") +
       ",\"notes\":" + q(Updater::notes) + ",\"error\":" + q(Updater::error) + ",\"checked\":" + q(Updater::lastCheckText) + "}";
  j += "}";
  sendJson(j);
}

// ---- Wecker -------------------------------------------------------------------------
inline String alarmsJson() {
  String j = "{\"master\":" + String(Alarm::master ? "true" : "false");
  j += ",\"snooze\":" + String(Alarm::snoozeMin) + ",\"melody\":" + String(Alarm::melody);
  j += ",\"text\":" + q(Alarm::text) + ",\"next\":" + q(Alarm::nextText());
  j += ",\"state\":" + q(Alarm::state == Alarm::RINGING ? "ringing" : Alarm::state == Alarm::SNOOZING ? "snoozing" : "idle");
  j += ",\"alarms\":[";
  for (int i = 0; i < Alarm::COUNT; i++) {
    const Alarm::Entry &e = Alarm::alarms[i];
    if (i) j += ",";
    j += "{\"on\":" + String(e.on ? "true" : "false") + ",\"hour\":" + String(e.hour) + ",\"minute\":" + String(e.minute) + ",\"days\":" + String(e.days) +
         ",\"sn\":" + String(e.snooze ? "true" : "false") + "}";
  }
  return j + "]}";
}

inline void handleAlarmsGet() { if (!requireAuth()) return; sendJson(alarmsJson()); }

inline void handleAlarmsSave() {
  if (!requireAuth()) return;
  for (int i = 0; i < Alarm::COUNT; i++) {
    String p = "a" + String(i) + "_";
    Alarm::Entry &e = Alarm::alarms[i];
    if (server.hasArg(p + "on")) e.on = server.arg(p + "on") == "1";
    if (server.hasArg(p + "time")) {
      String t = server.arg(p + "time");          // "07:30"
      int c = t.indexOf(':');
      if (c > 0) { e.hour = constrain(t.substring(0, c).toInt(), 0, 23); e.minute = constrain(t.substring(c + 1).toInt(), 0, 59); }
    }
    if (server.hasArg(p + "days")) e.days = server.arg(p + "days").toInt() & 0x7F;
    if (server.hasArg(p + "sn")) e.snooze = server.arg(p + "sn") == "1";
  }
  if (server.hasArg("master")) Alarm::master = server.arg("master") == "1";
  if (server.hasArg("snooze")) Alarm::snoozeMin = constrain(server.arg("snooze").toInt(), 1, 30);
  if (server.hasArg("melody")) Alarm::melody = constrain(server.arg("melody").toInt(), 0, 2);
  if (server.hasArg("text")) { String t = server.arg("text"); t.trim(); Alarm::text = t.substring(0, 120); }
  Alarm::save();
  sendJson(alarmsJson());
}

inline void handleAlarmAction() {
  if (!requireAuth()) return;
  String a = server.arg("action");
  if (a == "test") Alarm::startRing(0, true);
  else if (a == "snooze") Alarm::snooze();
  else { Alarm::stop(); App::scroller.stop(); }
  ok();
}

// Uhrzeit vom Handy übernehmen (Sekunden seit 1970, UTC)
inline void handleTime() {
  if (!requireAuth()) return;
  long long epoch = atoll(server.arg("epoch").c_str());
  if (epoch < 1700000000LL) { fail("Ungültige Zeit"); return; }
  struct timeval tv = {(time_t)epoch, 0};
  settimeofday(&tv, nullptr);
  ok();
}

// ---- Updates von der eigenen Webseite ------------------------------------------------------
inline void handleFwSettings() {
  if (!requireAuth()) return;
  String url = server.arg("url"); url.trim();
  if (url.length() && !url.startsWith("http://") && !url.startsWith("https://")) { fail("Adresse muss mit https:// beginnen"); return; }
  Settings::saveUpdate(url, server.arg("auto") == "1");
  ok();
}

inline void handleFwCheck() {
  if (!requireAuth()) return;
  bool okc = Updater::check();
  String j = "{\"ok\":" + String(okc ? "true" : "false") + ",\"latest\":" + q(Updater::latest) +
             ",\"available\":" + String(Updater::available ? "true" : "false") + ",\"notes\":" + q(Updater::notes) +
             ",\"error\":" + q(Updater::error) + ",\"current\":" + q(FW_VERSION) + "}";
  sendJson(j);
}

inline void handleFwProgress() {
  if (!requireAuth()) return;
  sendJson(String("{\"installing\":") + (Updater::installing ? "true" : "false") + ",\"restart\":" + (Updater::restarting ? "true" : "false") +
           ",\"pct\":" + Net::otaPercent + ",\"error\":" + q(Updater::installing ? String("") : Updater::error) + "}");
}

inline void handleFwInstall() {
  if (!requireAuth()) return;
  if (Updater::installing) { ok(); return; }
  if (!Updater::available) { fail("Kein Update verfügbar – erst prüfen"); return; }
  Updater::installRequested = true;
  ok();
}

// ---- Flugmodus ------------------------------------------------------------------------
inline int parseHm(const String &s, int def);
// ---- Tag- und Nachtprofil ----------------------------------------------------------------
// Welches Profil eine Anfrage meint: ?prof=0/1, sonst das gerade aktive
inline int profArg() {
  if (server.hasArg("prof")) return server.arg("prof") == "1" ? 1 : 0;
  return Profile::active;
}
inline String hm(uint16_t m) { char b[8]; snprintf(b, sizeof(b), "%02d:%02d", (m / 60) % 24, m % 60); return b; }
inline String profJson(const Profile::P &x) {
  return String("{\"bright\":") + x.bright + ",\"dark\":" + (x.dark ? "true" : "false") + ",\"blink\":" + (x.blink ? "true" : "false") +
         ",\"sound\":" + (x.sound ? "true" : "false") + ",\"flight\":" + (x.flight ? "true" : "false") + ",\"radius\":" + x.radius +
         ",\"maxAlt\":" + x.maxAlt + ",\"pages\":" + x.pages + ",\"maxPlanes\":" + x.maxPlanes + ",\"gap\":" + x.gap +
         ",\"bar\":" + (x.bar ? "true" : "false") + ",\"iss\":" + (x.iss ? "true" : "false") + ",\"issAnn\":" + (x.issAnnounce ? "true" : "false") +
         ",\"issBeep\":" + (x.issBeep ? "true" : "false") + "}";
}
inline String profileJson() {
  int64_t rise = 0, set = 0; Profile::sunTimes(rise, set);
  auto clock = [](int64_t t) -> String { if (!t) return ""; time_t v = (time_t)t; struct tm tm; localtime_r(&v, &tm); char b[8]; snprintf(b, sizeof(b), "%02d:%02d", tm.tm_hour, tm.tm_min); return b; };
  return "{\"mode\":" + String(Profile::mode) + ",\"from\":" + q(hm(Profile::from)) + ",\"to\":" + q(hm(Profile::to)) +
         ",\"active\":" + String(Profile::active) + ",\"sunrise\":" + q(clock(rise)) + ",\"sunset\":" + q(clock(set)) +
         ",\"p\":[" + profJson(Profile::p[0]) + "," + profJson(Profile::p[1]) + "]}";
}
inline void handleProfileGet() { if (!requireAuth()) return; sendJson(profileJson()); }
// Profilfelder aus einer Anfrage übernehmen (gemeinsam für /api/profile, /api/flight, /api/iss, /api/settings)
inline bool takeProfileArgs(int i) {
  Profile::P &x = Profile::p[i];
  Profile::P before = x;
  auto b = [](const char *k, bool &v) { if (server.hasArg(k)) v = server.arg(k) == "1"; };
  if (server.hasArg("bright")) x.bright = constrain(server.arg("bright").toInt(), 0, 7);
  if (server.hasArg("brightness")) x.bright = constrain(server.arg("brightness").toInt(), 0, 7);
  b("dark", x.dark); b("blink", x.blink); b("sound", x.sound);
  b("flight", x.flight); b("enabled", x.flight);
  if (server.hasArg("radius")) x.radius = constrain(server.arg("radius").toInt(), 1, 50);
  if (server.hasArg("maxAlt")) x.maxAlt = constrain(server.arg("maxAlt").toInt(), 0, 60000);
  if (server.hasArg("pages")) x.pages = server.arg("pages").toInt() & 0xFF;
  if (server.hasArg("maxPlanes")) x.maxPlanes = constrain(server.arg("maxPlanes").toInt(), 1, 8);
  if (server.hasArg("gap")) x.gap = constrain(server.arg("gap").toInt(), 3, 60);
  b("bar", x.bar); b("iss", x.iss); b("issAnn", x.issAnnounce); b("issBeep", x.issBeep);
  if (memcmp(&before, &x, sizeof(x)) == 0) return false;
  Profile::changed(i);
  return true;
}
inline void handleProfileSave() {
  if (!requireAuth()) return;
  if (server.hasArg("mode")) Profile::mode = constrain(server.arg("mode").toInt(), 0, 2);
  if (server.hasArg("from")) Profile::from = parseHm(server.arg("from"), Profile::from);
  if (server.hasArg("to")) Profile::to = parseHm(server.arg("to"), Profile::to);
  if (server.hasArg("mode") || server.hasArg("from") || server.hasArg("to")) { Profile::saveMode(); Profile::forced = true; }
  if (server.hasArg("prof")) takeProfileArgs(profArg());
  Profile::update();
  if (Display::brightness != Settings::d.brightness) Display::setBrightness(Settings::d.brightness);
  sendJson(profileJson());
}

inline String flightJson() {
  static Flight::Plane list[Flight::MAX];
  int n = Flight::snapshot(list, Flight::MAX);
  const Flight::Config &c = Flight::cfg;
  const Profile::P &pp = Profile::p[profArg()];
  char num[32];
  String j = "{\"enabled\":" + String(pp.flight ? "true" : "false") + ",\"prof\":" + String(profArg()) + ",\"profActive\":" + String(Profile::active) + ",\"live\":" + String(Flight::cfg.enabled ? "true" : "false");
  j += ",\"active\":" + String(Flight::activeNow() ? "true" : "false");
  snprintf(num, sizeof(num), "%.5f", c.lat); j += ",\"lat\":" + String(num);
  snprintf(num, sizeof(num), "%.5f", c.lon); j += ",\"lon\":" + String(num);
  j += ",\"radius\":" + String(pp.radius) + ",\"maxAlt\":" + String(pp.maxAlt) + ",\"poll\":" + String(c.poll);
  j += ",\"pages\":" + String(pp.pages) + ",\"maxPlanes\":" + String(pp.maxPlanes) + ",\"hideGround\":" + String(c.hideGround ? "true" : "false");
  j += ",\"schedule\":" + String(c.schedule ? "true" : "false");
  snprintf(num, sizeof(num), "\"%02d:%02d\"", c.fromMin / 60, c.fromMin % 60); j += ",\"from\":" + String(num);
  snprintf(num, sizeof(num), "\"%02d:%02d\"", c.toMin / 60, c.toMin % 60); j += ",\"to\":" + String(num);
  j += ",\"age\":" + String(Flight::updatedAt ? (int)((millis() - Flight::updatedAt) / 1000) : -1);
  String ferr, fsrc; Flight::statusText(ferr, fsrc);
  j += ",\"error\":" + q(ferr) + ",\"source\":" + q(fsrc) + ",\"routes\":" + q(Flight::routeText()) + ",\"req\":" + String(Fetch::requestsLastHour()) + ",\"pause\":" + Flight::backoffJson() + ",\"log\":" + Flight::logJson() + ",\"gap\":" + String(pp.gap) + ",\"bar\":" + String(pp.bar ? "true" : "false");
  j += ",\"planes\":[";
  for (int i = 0; i < n; i++) {
    const Flight::Plane &p = list[i];
    if (i) j += ",";
    j += "{\"call\":" + q(p.call) + ",\"hex\":" + q(p.hex) + ",\"type\":" + q(p.type) + ",\"reg\":" + q(p.reg);
    j += ",\"from\":" + q(p.from) + ",\"to\":" + q(p.to);
    j += ",\"alt\":" + String(p.ground ? 0 : p.altFt) + ",\"ground\":" + String(p.ground ? "true" : "false");
    snprintf(num, sizeof(num), "%.1f", p.distKm); j += ",\"dist\":" + String(num);
    j += ",\"gs\":" + String(p.gsKmh) + ",\"rate\":" + String(p.rateFpm) + ",\"bearing\":" + String(p.bearing) + ",\"elev\":" + String(p.elev);
    j += ",\"squawk\":" + String(p.squawk);
    Flight::Special sp = Flight::special(p);
    if (sp.prio) j += ",\"tag\":" + q(sp.text) + ",\"prio\":" + String(sp.prio);
    if (p.dbFlags & 1) j += ",\"mil\":true";
    snprintf(num, sizeof(num), "%.5f", p.lat); j += ",\"lat\":" + String(num);
    snprintf(num, sizeof(num), "%.5f", p.lon); j += ",\"lon\":" + String(num);
    j += "}";
  }
  return j + "]}";
}

inline void handleFlightGet() { if (!requireAuth()) return; sendJson(flightJson()); }

inline int parseHm(const String &s, int def) {
  int c = s.indexOf(':');
  if (c <= 0) return def;
  return constrain(s.substring(0, c).toInt(), 0, 23) * 60 + constrain(s.substring(c + 1).toInt(), 0, 59);
}

inline void handleFlightSave() {
  if (!requireAuth()) return;
  Flight::Config c = Flight::cfg;
  if (server.hasArg("lat")) { double v = server.arg("lat").toDouble(); if (v < -90 || v > 90) { fail("Breite muss zwischen -90 und 90 liegen"); return; } c.lat = v; }
  if (server.hasArg("lon")) { double v = server.arg("lon").toDouble(); if (v < -180 || v > 180) { fail("Länge muss zwischen -180 und 180 liegen"); return; } c.lon = v; }
  if (server.hasArg("poll")) c.poll = constrain(server.arg("poll").toInt(), 0, 3);
  if (server.hasArg("hideGround")) c.hideGround = server.arg("hideGround") == "1";
  if (server.hasArg("schedule")) c.schedule = server.arg("schedule") == "1";
  if (server.hasArg("from")) c.fromMin = parseHm(server.arg("from"), c.fromMin);
  if (server.hasArg("to")) c.toMin = parseHm(server.arg("to"), c.toMin);
  Flight::setConfig(c);                        // Standort, Abfrage, Boden, Zeitplan
  takeProfileArgs(profArg());                  // An/aus, Radius, Höhe, Seiten, Anzahl, Pause, Punkte
  Profile::update();
  sendJson(flightJson());
}

inline void handleFrame() {
  if (!requireAuth()) return;
  sendJson("{\"hx\":\"" + Display::snapshotHex() + "\"}");
}

inline void handleSettings() {
  if (!requireAuth()) return;
  takeProfileArgs(profArg());                  // Helligkeit, Anzeige aus, Doppelpunkt, Töne
  Profile::update();
  if (server.hasArg("intro")) Settings::saveIntro(server.arg("intro") == "1");
  if (server.hasArg("rotate")) { Settings::saveRotate(server.arg("rotate") == "1"); Display::rotate180 = Settings::d.rotate180; Display::dirty = true; }
  if (server.hasArg("greeting")) { String g = server.arg("greeting"); g.trim(); Settings::saveGreeting(g.substring(0, 120)); }
  if (server.hasArg("tz")) {
    String tz = server.arg("tz"); tz.trim();
    if (tz.length()) { Settings::saveTz(tz); configTzTime(tz.c_str(), NTP_SERVER_1, NTP_SERVER_2); }
  }
  if (server.hasArg("host")) {
    String h = server.arg("host"); h.trim(); h.toLowerCase();
    bool valid = h.length() >= 2 && h.length() <= 24;
    for (char c : h) if (!isalnum((unsigned char)c) && c != '-') valid = false;
    if (!valid) { fail("Gerätename: 2–24 Zeichen, nur a–z, 0–9 und -"); return; }
    if (h != Settings::d.hostname) { Settings::saveHostname(h); rebootAt = millis() + 1500; }
  }
  ok();
}

inline void handleSecurity() {
  if (!requireAuth()) return;
  if (server.hasArg("ideOta")) Net::setIdeOta(server.arg("ideOta") == "1");
  if (server.arg("newAp") == "1") Settings::newSetupPass();
  sendJson("{\"ok\":true,\"apPass\":" + q(Settings::d.apPass) + "}");
}

inline void handleDots() {
  if (!requireAuth()) return;
  uint8_t r[4];
  const char *names[4] = {"w1", "w2", "snooze", "flight"};
  for (int i = 0; i < 4; i++) {
    int v = server.hasArg(names[i]) ? server.arg(names[i]).toInt() : Settings::d.dotRole[i];
    r[i] = (v >= 0 && v < Display::DOT_COUNT) ? (uint8_t)v : 255;
  }
  Settings::saveDots(r);
  ok();
}

// ---- ISS ------------------------------------------------------------------------------------
inline String issJson() {
  static Sat::Pass list[Iss::MAXP];
  int n = Iss::list(list, Iss::MAXP);
  const Iss::Config &c = Iss::cfg;
  const Profile::P &pp = Profile::p[profArg()];
  int64_t now = Net::timeValid() ? (int64_t)time(nullptr) : 0;
  String j = "{\"enabled\":" + String(pp.iss ? "true" : "false") + ",\"live\":" + String(c.enabled ? "true" : "false") + ",\"minEl\":" + String(c.minEl) +
             ",\"visibleOnly\":" + String(c.visibleOnly ? "true" : "false") + ",\"announce\":" + String(pp.issAnnounce ? "true" : "false") +
             ",\"beep\":" + String(pp.issBeep ? "true" : "false") + ",\"haveTle\":" + String(Iss::haveModel ? "true" : "false");
  j += ",\"tleAgeH\":" + String(Iss::tleAt && now ? (int)((now - Iss::tleAt) / 3600) : -1);
  j += ",\"error\":" + q(Iss::error) + ",\"now\":" + String((long)now);
  j += ",\"passes\":[";
  for (int i = 0; i < n; i++) {
    const Sat::Pass &p = list[i];
    char b[200];
    snprintf(b, sizeof(b), "%s{\"start\":%ld,\"end\":%ld,\"peak\":%ld,\"maxEl\":%.0f,\"azS\":%.0f,\"azE\":%.0f,\"from\":\"%s\",\"to\":\"%s\",\"vis\":%s}",
             i ? "," : "", (long)p.start, (long)p.end, (long)p.peak, p.maxEl, p.azStart, p.azEnd,
             Sat::compass8(p.azStart), Sat::compass8(p.azEnd), p.visible ? "true" : "false");
    j += b;
  }
  return j + "]}";
}
// ---- Extras: besondere Tage, geheime Nachricht, Schalter, Timer, Statistik -----------------
inline void handleExtrasGet() { if (!requireAuth()) return; sendJson(Extras::json()); }
inline void handleExtrasSave() {
  if (!requireAuth()) return;
  auto flag = [](const char *k, bool &v) { if (server.hasArg(k)) v = server.arg(k) == "1"; };
  flag("wakeWeather", Extras::wakeWeather); flag("overhead", Extras::overhead); flag("lightAlarm", Extras::lightAlarm); flag("song", Extras::song);
  if (server.hasArg("secret")) Extras::secret = Extras::clean(server.arg("secret"), 200);
  if (server.hasArg("days")) {
    // Zeilen „TT.MM|Tage vorher|Name|Text“ – Name/Text werden bereinigt
    String in = server.arg("days"), out;
    int pos = 0;
    while (pos < (int)in.length()) {
      int nl = in.indexOf('\n', pos);
      String line = in.substring(pos, nl < 0 ? in.length() : nl);
      pos = nl < 0 ? in.length() : nl + 1;
      int a = line.indexOf('|'), b = line.indexOf('|', a + 1), c = line.indexOf('|', b + 1);
      if (a != 5 || b < 0 || c < 0) continue;
      out += line.substring(0, b + 1) + Extras::clean(line.substring(b + 1, c), 40) + "|" + Extras::clean(line.substring(c + 1), 160) + "\n";
    }
    Extras::parseDays(out);
  }
  Extras::save();
  sendJson(Extras::json());
}
// ---- Flug verfolgen, Abzeichen, Unwetter, Ständchen ---------------------------------------
inline void handleTrack() {
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST) {
    if (server.arg("a") == "stop") Tracker::stop();
    else if (server.hasArg("call")) {
      String c = Tracker::cleanCall(server.arg("call"));
      if (c.length() < 3) { fail("Bitte eine Flugnummer bzw. ein Rufzeichen eingeben, z. B. EWG9AT"); return; }
      Tracker::start(c);
      Flight::refreshNow = true;
    }
  }
  sendJson(Tracker::json());
}
inline void handleBadges() {
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST && server.arg("a") == "reset") Badges::reset();
  sendJson(Badges::json());
}
inline void handleAlerts() {
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST) {
    if (server.hasArg("enabled")) { Alerts::enabled = server.arg("enabled") == "1"; Alerts::refresh = true; }
    if (server.hasArg("min")) { Alerts::minLevel = constrain(server.arg("min").toInt(), 1, 4); Alerts::refresh = true; }
    Alerts::save();
  }
  sendJson(Alerts::json());
}
inline void handleAlbum() {
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST && server.arg("a") == "reset") { Album::reset(); ok(); return; }
  String part = server.arg("part");
  if (part == "airlines") sendJson(Album::jsonAirlines());
  else if (part == "airports") sendJson(Album::jsonAirports());
  else sendJson(Album::jsonTypes());
}
inline void handleSong() {
  if (!requireAuth()) return;
  App::playSong(true);
  App::message("\x05 HAPPY BIRTHDAY! \x05", 2);
  ok();
}

inline void handleTimer() {
  if (!requireAuth()) return;
  String a = server.arg("a");
  if (a == "start") Extras::timerStart(constrain(server.arg("sec").toInt(), 0, 99 * 60));
  else if (a == "add") Extras::timerAdd(server.arg("min").toInt());
  else if (a == "stop") { Extras::timerStop(); noTone(Pin::BUZZER); }
  App::wakeUntil = millis() + 15000;
  sendJson(Extras::json());
}
inline void handleStats() {
  if (!requireAuth()) return;
  if (server.method() == HTTP_POST && server.arg("a") == "reset") Stats::reset();
  sendJson(Stats::json());
}

// ---- Einstellungen sichern / zurückspielen -----------------------------------------------
// Alle Einträge des Speichers „flyglinje“ außer Zugangsdaten, WLAN, Geräte-Schlüssel und
// Laufzeit-Zählern. Format: {"fw":"0.7.4","v":{"bright":["u8",2],"prof0":["b","0a1b…"],…}}
inline bool backupSkip(const char *k) {
  static const char *SKIP[] = {"dsec", "wpx", "apx", "otax", "user", "salt", "hash", "hver", "deflogin", "ssid",
                               "boots", "keys", "rlog", "tryfrom", "lastver", "is_l1", "is_l2", "is_at"};
  for (auto s : SKIP) if (strcmp(k, s) == 0) return true;
  return false;
}
inline String jsonStr(const char *s) {
  String o = "\"";
  for (const char *p = s; *p; p++) {
    char c = *p;
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if (c == '\n') o += "\\n";
    else if ((uint8_t)c < 0x20) o += ' ';
    else o += c;
  }
  return o + "\"";
}
inline void handleBackup() {
  if (!requireAuth()) return;
  nvs_handle_t h;
  if (nvs_open("flyglinje", NVS_READONLY, &h) != ESP_OK) { fail("Speicher nicht lesbar"); return; }
  String j = "{\"fw\":\"" FW_VERSION "\",\"v\":{";
  bool first = true;
  nvs_iterator_t it = nullptr;
  esp_err_t e = nvs_entry_find("nvs", "flyglinje", NVS_TYPE_ANY, &it);
  while (e == ESP_OK) {
    nvs_entry_info_t info; nvs_entry_info(it, &info);
    const char *k = info.key;
    String val;
    if (!backupSkip(k)) switch (info.type) {
      case NVS_TYPE_U8: { uint8_t v; if (nvs_get_u8(h, k, &v) == ESP_OK) val = "[\"u8\"," + String(v) + "]"; break; }
      case NVS_TYPE_I8: { int8_t v; if (nvs_get_i8(h, k, &v) == ESP_OK) val = "[\"i8\"," + String(v) + "]"; break; }
      case NVS_TYPE_U16: { uint16_t v; if (nvs_get_u16(h, k, &v) == ESP_OK) val = "[\"u16\"," + String(v) + "]"; break; }
      case NVS_TYPE_I16: { int16_t v; if (nvs_get_i16(h, k, &v) == ESP_OK) val = "[\"i16\"," + String(v) + "]"; break; }
      case NVS_TYPE_U32: { uint32_t v; if (nvs_get_u32(h, k, &v) == ESP_OK) val = "[\"u32\"," + String(v) + "]"; break; }
      case NVS_TYPE_I32: { int32_t v; if (nvs_get_i32(h, k, &v) == ESP_OK) val = "[\"i32\"," + String(v) + "]"; break; }
      case NVS_TYPE_I64: { int64_t v; if (nvs_get_i64(h, k, &v) == ESP_OK) { char b[24]; snprintf(b, sizeof(b), "%lld", (long long)v); val = "[\"i64\"," + String(b) + "]"; } break; }
      case NVS_TYPE_STR: {
        size_t n = 0;
        if (nvs_get_str(h, k, nullptr, &n) == ESP_OK && n < 2048) { char *b = (char *)malloc(n); if (b && nvs_get_str(h, k, b, &n) == ESP_OK) val = "[\"s\"," + jsonStr(b) + "]"; free(b); }
        break;
      }
      case NVS_TYPE_BLOB: {
        size_t n = 0;
        if (nvs_get_blob(h, k, nullptr, &n) == ESP_OK && n <= 1024) {
          uint8_t *b = (uint8_t *)malloc(n ? n : 1);
          if (b && nvs_get_blob(h, k, b, &n) == ESP_OK) {
            static const char *hx = "0123456789abcdef";
            String s; s.reserve(n * 2);
            for (size_t i = 0; i < n; i++) { s += hx[b[i] >> 4]; s += hx[b[i] & 15]; }
            val = "[\"b\",\"" + s + "\"]";
          }
          free(b);
        }
        break;
      }
      default: break;
    }
    if (val.length()) { if (!first) j += ","; first = false; j += "\"" + String(k) + "\":" + val; }
    e = nvs_entry_next(&it);
  }
  nvs_release_iterator(it);
  nvs_close(h);
  j += "}}";
  server.sendHeader("Content-Disposition", "attachment; filename=\"flyglinje-einstellungen.json\"");
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

// Einfacher Leser für genau das Format oben
inline void handleRestore() {
  if (!requireAuth()) return;
  String d = server.arg("data");
  int v = d.indexOf("\"v\":{");
  if (v < 0) { fail("Keine FLYGLINJE-Sicherung"); return; }
  nvs_handle_t h;
  if (nvs_open("flyglinje", NVS_READWRITE, &h) != ESP_OK) { fail("Speicher nicht beschreibbar"); return; }
  int p = v + 5, done = 0;
  auto readStr = [&](int &i) -> String {        // i steht auf dem öffnenden "
    String o; i++;
    while (i < (int)d.length() && d[i] != '"') {
      if (d[i] == '\\' && i + 1 < (int)d.length()) { i++; o += d[i] == 'n' ? '\n' : d[i]; } else o += d[i];
      i++;
    }
    i++; return o;
  };
  while (true) {
    int q = d.indexOf('"', p);
    if (q < 0) break;
    String key = readStr(q);
    int lb = d.indexOf('[', q); if (lb < 0) break;
    int tq = d.indexOf('"', lb); String type = readStr(tq);
    int c = d.indexOf(',', tq); if (c < 0) break;
    int i = c + 1;
    while (d[i] == ' ') i++;
    p = i;
    if (key.length() > 15 || backupSkip(key.c_str())) {          // überspringen (Text kann ] enthalten)
      if (type == "s" || type == "b") readStr(i);
      int rb = d.indexOf(']', i); if (rb < 0) break; p = rb + 1; continue;
    }
    if (type == "s" || type == "b") {
      String s = readStr(i); p = i;
      if (type == "s") nvs_set_str(h, key.c_str(), s.c_str());
      else {
        size_t n = s.length() / 2; uint8_t *b = (uint8_t *)malloc(n ? n : 1);
        for (size_t k = 0; k < n; k++) b[k] = (uint8_t)strtoul(s.substring(k * 2, k * 2 + 2).c_str(), nullptr, 16);
        nvs_set_blob(h, key.c_str(), b, n); free(b);
      }
    } else {
      long long x = atoll(d.c_str() + i);
      if (type == "u8") nvs_set_u8(h, key.c_str(), (uint8_t)x);
      else if (type == "i8") nvs_set_i8(h, key.c_str(), (int8_t)x);
      else if (type == "u16") nvs_set_u16(h, key.c_str(), (uint16_t)x);
      else if (type == "i16") nvs_set_i16(h, key.c_str(), (int16_t)x);
      else if (type == "u32") nvs_set_u32(h, key.c_str(), (uint32_t)x);
      else if (type == "i32") nvs_set_i32(h, key.c_str(), (int32_t)x);
      else if (type == "i64") nvs_set_i64(h, key.c_str(), (int64_t)x);
      else { int rb = d.indexOf(']', i); p = rb + 1; continue; }
    }
    done++;
    int rb = d.indexOf(']', p); if (rb < 0) break;
    p = rb + 1;
    if (d.indexOf('"', p) < 0 || d.indexOf('}', p) < d.indexOf('"', p)) break;   // Ende von "v"
  }
  nvs_commit(h); nvs_close(h);
  sendJson("{\"ok\":true,\"count\":" + String(done) + "}");
  rebootAt = millis() + 1500;                    // neu starten, damit alles geladen wird
}

// ---- Wetter ------------------------------------------------------------------------------
inline void handleWeatherGet() { if (!requireAuth()) return; sendJson(Weather::json()); }
inline void handleWeatherSave() {
  if (!requireAuth()) return;
  if (server.hasArg("enabled")) { bool on = server.arg("enabled") == "1"; if (on && !Weather::enabled) Weather::refresh = true; Weather::enabled = on; }
  if (server.hasArg("every")) { int e = server.arg("every").toInt(); if (e == 1 || e == 2 || e == 5 || e == 10) Weather::everyMin = e; }
  Weather::save();
  sendJson(Weather::json());
}

inline void handleIssGet() { if (!requireAuth()) return; sendJson(issJson()); }
inline void handleIssSave() {
  if (!requireAuth()) return;
  Iss::Config c = Iss::cfg;
  if (server.hasArg("minEl")) c.minEl = constrain(server.arg("minEl").toInt(), 10, 80);
  if (server.hasArg("visibleOnly")) c.visibleOnly = server.arg("visibleOnly") == "1";
  Iss::setConfig(c);
  // An/aus, Ankündigung, Ton gehören zum Profil (gleiche Feldnamen wie bisher)
  if (server.hasArg("enabled")) { Profile::p[profArg()].iss = server.arg("enabled") == "1"; Profile::changed(profArg()); }
  if (server.hasArg("announce")) { Profile::p[profArg()].issAnnounce = server.arg("announce") == "1"; Profile::changed(profArg()); }
  if (server.hasArg("beep")) { Profile::p[profArg()].issBeep = server.arg("beep") == "1"; Profile::changed(profArg()); }
  Profile::update();
  sendJson(issJson());
}

// ---- Spiele ------------------------------------------------------------------------------
// Quartett-Stand aus Sicht eines Spielers: nur die eigene oberste Karte ist sichtbar
inline String qCard(int i) {
  if (i < 0) return "null";
  return String("{\"code\":") + q(QtData::ROWS[i].code) + ",\"name\":" + q(Game::Q::name(i)) + ",\"r\":" + Game::Q::rar(i) + "}";
}
inline String qJson(bool guest) {
  using namespace Game;
  int me = guest ? 1 : 0;
  String j = String(",\"q\":{\"me\":") + me + ",\"turn\":" + Q::turn + ",\"phase\":" + Q::phase + ",\"n\":[" + Q::dn[0] + "," + Q::dn[1] +
             "],\"pot\":" + Q::potN + ",\"per\":" + Q::per + ",\"round\":" + Q::rounds + ",\"over\":" + (Q::over ? "true" : "false") +
             ",\"card\":" + qCard(Q::top(me));
  if (Q::lastCat >= 0) {
    j += ",\"last\":{\"cat\":" + String(Q::lastCat) + ",\"win\":" + Q::lastWin + ",\"round\":" + Q::rounds + ",\"cards\":[" + qCard(Q::lastCard[0]) + "," + qCard(Q::lastCard[1]) + "]}";
  }
  return j + "}";
}
inline String gameJson(bool guest = false) {
  const char *k = Game::kind == Game::SNAKE ? "snake" : Game::kind == Game::PONG ? "pong" : Game::kind == Game::QUARTETT ? "quartett" : "";
  return String("{\"game\":\"") + k + "\",\"running\":" + (Game::active() ? "true" : "false") +
         ",\"score\":[" + Game::score[0] + "," + Game::score[1] + "],\"high\":" + Game::highSnake +
         ",\"two\":" + (Game::twoPlayer ? "true" : "false") + ",\"last\":" + q(Game::endText) +
         ",\"lag\":" + String(Diag::stallRecent()) + ",\"lagAt\":" + q(Diag::stallWhere) +
         ",\"wait\":" + (Game::waitP2 ? "true" : "false") + ",\"guest\":" + (guest ? "true" : "false") +
         (guest ? String("") : String(",\"qcards\":") + Game::Q::count()) +
         (Game::kind == Game::QUARTETT ? qJson(guest) : String("")) + "}";
}
inline void handleGameGet() { bool guest; if (!requireGameAuth(guest)) return; sendJson(gameJson(guest)); }
inline void handleGamePost() {
  bool guest;
  if (!requireGameAuth(guest)) return;
  String a = server.arg("a");
  if (guest) {                               // Spieler 2: nur die rechte Seite steuern oder aufhören
    if (a == "pos") Game::position(1, server.arg("v").toInt());
    else if (a == "hl") Game::highlight(1);
    else if (a == "pick") Game::qPick(1, server.arg("c").toInt());
    else if (a == "next") Game::qNext();
    else if (a == "leave" && Game::active()) Game::finish("SPIELER 2 IST WEG");
    App::wakeUntil = millis() + 15000;
    sendJson(gameJson(true));
    return;
  }
  if (a == "start" && server.arg("game") == "quartett") {
    App::stopTest();
    guestToken = "";
    if (!Game::startQ(server.arg("n").toInt())) { fail("Für Quartett brauchst du mindestens 10 Flugzeugtypen im Album"); return; }
    WiFi.setSleep(WIFI_PS_NONE);
  } else if (a == "pick") {
    Game::qPick(0, server.arg("c").toInt());
  } else if (a == "next") {
    Game::qNext();
  } else if (a == "start") {
    String g = server.arg("game");
    App::stopTest();
    guestToken = "";
    Game::start(g == "pong" ? Game::PONG : Game::SNAKE, server.arg("two") == "1", server.arg("wait") == "1");
    WiFi.setSleep(WIFI_PS_NONE);             // während des Spiels ohne WLAN-Schlaf: schnellere Reaktion
  } else if (a == "dir") {
    String d = server.arg("d");
    if (d.length()) Game::input(d[0]);
  } else if (a == "pos") {
    Game::position(server.arg("side").toInt(), server.arg("v").toInt());
  } else if (a == "hl") {
    Game::highlight(server.arg("side").toInt());
  } else if (a == "stop") {
    if (Game::active()) Game::finish("SPIEL BEENDET");
  }
  App::wakeUntil = millis() + 15000;
  sendJson(gameJson(false));
}

inline void handleGuestGet() {
  if (!fromHome()) return;
  sendJson(String("{\"waiting\":") + (Game::waitP2 ? "true" : "false") + ",\"game\":\"" + (Game::kind == Game::QUARTETT ? "quartett" : "pong") + "\"}");
}
inline void handleGuestJoin() {
  if (!fromHome()) return;
  if (!Game::waitP2) { fail("Gerade wartet kein Spiel auf Spieler 2", 409); return; }
  // Quartett: Spieler 2 wird zu Spieler 2 (Index 1); Pong wie gehabt
  guestToken = Settings::randomHex(16);
  Game::joinP2();
  App::wakeUntil = millis() + 15000;
  server.sendHeader("Set-Cookie", "gid=" + guestToken + "; Path=/; HttpOnly; SameSite=Strict; Max-Age=3600");
  sendJson(gameJson(true));
}

// ---- Spiel-Steuerung über WebSocket (Port 81) ------------------------------------------------
// Eine dauerhaft offene Verbindung je Handy: Eingaben kommen ohne neuen Verbindungsaufbau
// sofort an (HTTP-Anfragen gingen bei schnellen Eingaben manchmal verloren). Die Uhr
// schickt den Spielstand von sich aus zurück. Bis zu 3 Handys gleichzeitig.
namespace Ws {
inline WiFiServer srv(81);
struct Client { WiFiClient c; bool open = false; bool guest = false; uint8_t buf[256]; size_t len = 0; String head; uint32_t since = 0, lastRx = 0; };
constexpr int MAXC = 3;
inline Client cl[MAXC];
inline uint32_t lastPush = 0;
inline bool pushNow = false;               // Quartett: Zug sofort an beide Handys

inline bool tokenOk(const String &cookie) {
  int i = cookie.indexOf("sid=");
  if (i < 0) return false;
  int e = cookie.indexOf(';', i);
  String t = cookie.substring(i + 4, e < 0 ? cookie.length() : e);
  if (t.length() != 32) return false;
  for (auto &s : sessions) if (s.token.length() && Settings::safeEquals(s.token, t)) return millis() - s.created <= SESSION_MS;
  return false;
}

inline void drop(Client &x) { x.c.stop(); x.open = false; x.len = 0; x.head = ""; }

// Senden ohne zu warten: WiFiClient::write wartet bei vollem Puffer bis zu 10 s – ein Handy,
// das gerade schläft oder die Seite verlassen hat, hielt so die ganze Uhr an (Spiele hingen
// 3–5 s). Passt ein Rahmen nicht sofort in den Puffer, wird die Verbindung geschlossen; die
// Seite baut sie von selbst neu auf.
inline bool rawSend(Client &x, const uint8_t *d, size_t n) {
  int fd = x.c.fd();
  if (fd < 0) return false;
  int r = ::send(fd, d, n, MSG_DONTWAIT);
  if (r == (int)n) return true;
  drop(x);
  return false;
}

inline void sendText(Client &x, const String &t) {
  if (!x.open) return;
  size_t n = t.length();
  if (n > 1000) return;
  uint8_t f[1004]; size_t hl = 2;
  f[0] = 0x81;
  if (n < 126) f[1] = (uint8_t)n; else { f[1] = 126; f[2] = n >> 8; f[3] = n & 0xFF; hl = 4; }
  memcpy(f + hl, t.c_str(), n);
  rawSend(x, f, hl + n);
}

inline void handshake(Client &x) {
  while (x.c.available() && x.head.length() < 2048) x.head += (char)x.c.read();
  if (x.head.indexOf("\r\n\r\n") < 0) { if (millis() - x.since > 3000) drop(x); return; }
  auto header = [&](const char *name) -> String {
    String low = x.head; low.toLowerCase();
    String key = String("\r\n") + name + ":";
    int i = low.indexOf(key);
    if (i < 0) return "";
    int s = i + key.length(), e = x.head.indexOf("\r\n", s);
    String v = x.head.substring(s, e); v.trim(); return v;
  };
  String key = header("sec-websocket-key");
  String ck = header("cookie");
  x.guest = false;
  if (!key.isEmpty() && privateIp(x.c.remoteIP()) && !tokenOk(ck) && guestValid(ck)) x.guest = true;
  else if (key.isEmpty() || !privateIp(x.c.remoteIP()) || !tokenOk(ck)) {
    x.c.print("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\n\r\n"); drop(x); return;
  }
  String in = key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t sha[20]; mbedtls_sha1((const uint8_t *)in.c_str(), in.length(), sha);
  uint8_t b64[40]; size_t ol = 0; mbedtls_base64_encode(b64, sizeof(b64), &ol, sha, 20); b64[ol] = 0;
  x.c.print(String("HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Accept: ") + (char *)b64 + "\r\n\r\n");
  x.open = true; x.head = ""; x.len = 0; x.lastRx = millis();
  sendText(x, gameJson(x.guest));
}

inline void message(const char *m, bool guest) {
  // "d U" = Richtung (Snake), "p 0 523" = Schlägerposition (Pong, Seite, 0..1000), "k" = Lebenszeichen
  if (m[0] == 'd' && m[1] == ' ' && m[2]) Game::input(m[2]);
  else if (m[0] == 'p' && m[1] == ' ') { int side = guest ? 1 : atoi(m + 2); const char *v = strchr(m + 2, ' '); if (v) Game::position(side, atoi(v + 1)); }
  else if (m[0] == 'h' && m[1] == ' ') Game::highlight(guest ? 1 : atoi(m + 2));
  else if (m[0] == 'q' && m[1] == ' ') { if (!Game::qPick(guest ? 1 : 0, atoi(m + 2))) return; pushNow = true; }
  else if (m[0] == 'n' && !m[1]) { Game::qNext(); pushNow = true; }
  else return;
  App::wakeUntil = millis() + 15000;
}

inline void frames(Client &x) {
  while (x.c.available() && x.len < sizeof(x.buf)) {
    int r = x.c.read(x.buf + x.len, sizeof(x.buf) - x.len);
    if (r <= 0) break;
    x.len += r;
  }
  while (x.len >= 2) {
    uint8_t op = x.buf[0] & 0x0F;
    bool masked = x.buf[1] & 0x80;
    size_t n = x.buf[1] & 0x7F, hl = 2;
    if (n == 126) { if (x.len < 4) return; n = (x.buf[2] << 8) | x.buf[3]; hl = 4; }
    else if (n == 127) { drop(x); return; }
    if (masked) hl += 4;
    if (n > 200) { drop(x); return; }
    if (x.len < hl + n) return;                 // Rahmen noch nicht vollständig
    uint8_t *mask = x.buf + hl - 4, *pl = x.buf + hl;
    if (masked) for (size_t i = 0; i < n; i++) pl[i] ^= mask[i & 3];
    if (op == 0x8) { drop(x); return; }          // schließen
    x.lastRx = millis();
    if (op == 0x9) { uint8_t f[204] = {0x8A, (uint8_t)n}; memcpy(f + 2, pl, n); if (!rawSend(x, f, n + 2)) return; }   // Ping -> Pong
    if (op == 0x1) { char m[64]; size_t k = n < 63 ? n : 63; memcpy(m, pl, k); m[k] = 0; message(m, x.guest); }
    memmove(x.buf, x.buf + hl + n, x.len - hl - n);
    x.len -= hl + n;
  }
}

inline void begin() { srv.begin(); srv.setNoDelay(true); }

inline void update() {
  WiFiClient n = srv.accept();
  if (n) {
    int slot = -1;
    for (int i = 0; i < MAXC; i++) if (!cl[i].c.connected()) { slot = i; break; }
    if (slot < 0) n.stop();
    else { drop(cl[slot]); cl[slot].c = n; cl[slot].c.setNoDelay(true); cl[slot].c.setTimeout(200); cl[slot].since = millis(); }
  }
  bool any = false;
  for (auto &x : cl) {
    if (!x.c.connected()) { if (x.open || x.head.length()) drop(x); continue; }
    if (!x.open) { handshake(x); continue; }
    // Die Seite schickt alle 5 s ein Lebenszeichen; 20 s Stille = Handy weg (gesperrt, Tab zu)
    if (millis() - x.lastRx > 20000) { drop(x); continue; }
    frames(x); if (x.open) any = true;
  }
  // Spielstand an alle Handys (bei laufendem Spiel 3× pro Sekunde, sonst alle 2 s)
  if (any && (pushNow || millis() - lastPush > (Game::active() ? 330u : 2000u))) {
    lastPush = millis(); pushNow = false;
    String j = gameJson(false), jg = gameJson(true);
    for (auto &x : cl) if (x.open) sendText(x, x.guest ? jg : j);
  }
}
}  // namespace Ws

inline void handlePower() {
  if (!requireAuth()) return;
  if (server.hasArg("save")) { Settings::savePowerSave(server.arg("save") == "1"); Net::applyPower(); }
  ok();
}

inline void handleMessage() {
  if (!requireAuth()) return;
  String t = server.arg("text"); t.trim();
  if (t.isEmpty()) { fail("Kein Text"); return; }
  int times = constrain(server.arg("times").toInt(), 1, 20);
  App::message(t.substring(0, 200), times);
  ok();
}

inline void handleScan() {
  if (!fromHome()) return;
  if (!firstSetup() && !requireAuth()) return;
  int n = WiFi.scanNetworks(false, false);
  String j = "{\"networks\":[";
  int count = 0;
  for (int i = 0; i < n && count < 25; i++) {
    String s = WiFi.SSID(i);
    if (s.isEmpty()) continue;
    bool dup = false;
    for (int k = 0; k < i; k++) if (WiFi.SSID(k) == s) dup = true;
    if (dup) continue;
    if (count++) j += ",";
    j += "{\"ssid\":" + q(s) + ",\"rssi\":" + String(WiFi.RSSI(i)) + ",\"open\":" + String(WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "true" : "false") + "}";
  }
  j += "]}";
  WiFi.scanDelete();
  sendJson(j);
}

inline void handleWifi() {
  if (!fromHome()) return;
  if (!firstSetup() && !requireAuth()) return;
  String ssid = server.arg("ssid"), pass = server.arg("pass");
  ssid.trim();
  if (ssid.isEmpty()) { fail("Bitte ein WLAN auswählen"); return; }
  if (Net::tryConnect(ssid, pass)) {
    sendJson("{\"ok\":true,\"ip\":" + q(WiFi.localIP().toString()) + ",\"host\":" + q(Settings::d.hostname) + "}");
  } else {
    fail("Verbindung fehlgeschlagen. Passwort prüfen?");
  }
}

inline void handleReboot() {
  if (!requireAuth()) return;
  ok(); rebootAt = millis() + 800;
}

inline void handleFactory() {
  if (!requireAuth()) return;
  Settings::factoryReset();
  Album::reset();
  ok(); rebootAt = millis() + 800;
}

inline void handleTest() {
  if (!requireAuth()) return;
  String m = server.arg("mode");
  if (m == "all") App::startTest(App::TEST_ALL);
  else if (m == "chip") App::startTest(App::TEST_CHIP, constrain(server.arg("chip").toInt(), 0, 2));
  else if (m == "scan") App::startTest(App::TEST_SCAN);
  else if (m == "dots") App::startTest(App::TEST_DOTS, server.hasArg("dot") ? constrain(server.arg("dot").toInt(), -1, 4) : -1);
  else if (m == "text") App::startTest(App::TEST_TEXT, 0, server.arg("text"));
  else if (m == "flight") App::startTest(App::TEST_FLIGHT, constrain(server.arg("kind").toInt(), 0, Flight::DEMO_COUNT - 1));
  else if (m == "beep") Buzzer::beep(server.arg("hz").toInt() > 0 ? server.arg("hz").toInt() : 2000, 300, true);
  else App::stopTest();
  ok();
}

inline void handleKeys() {
  if (!requireAuth()) return;
  String j = "{\"top\":" + String(Keys::mvTop) + ",\"back\":" + String(Keys::mvBack) + ",\"key\":" + q(Keys::name(Keys::down)) +
             ",\"topOk\":" + String(Keys::connected(0) ? "true" : "false") + ",\"backOk\":" + String(Keys::connected(1) ? "true" : "false") + "}";
  sendJson(j);
}

// ---- Firmware-Update über den Browser ----------------------------------------------
inline void handleUpdateDone() {
  if (!authed()) { fail("Bitte anmelden", 401); return; }
  if (updateOk) { ok(); rebootAt = millis() + 1000; }
  else { fail(updateError.length() ? updateError : String("Update fehlgeschlagen")); }
}

inline void handleUpdateUpload() {
  HTTPUpload &up = server.upload();
  if (up.status == UPLOAD_FILE_START) {
    updateOk = false; updateError = "";
    if (!authed()) { updateError = "Bitte anmelden"; return; }
    Net::otaRunning = true; Net::otaPercent = 0;
    if (!Update.begin(UPDATE_SIZE_UNKNOWN, U_FLASH)) updateError = "Kein Platz für das Update (Partitionsschema prüfen)";
  } else if (up.status == UPLOAD_FILE_WRITE) {
    if (updateError.length()) return;
    if (up.totalSize == 0 && up.currentSize > 0 && up.buf[0] != 0xE9) { updateError = "Das ist keine ESP32-Firmware (.bin)"; Update.abort(); return; }
    if (Update.write(up.buf, up.currentSize) != up.currentSize) updateError = "Schreibfehler beim Update";
    size_t expected = server.header("Content-Length").toInt();
    if (expected) Net::otaPercent = min(99, (int)((up.totalSize + up.currentSize) * 100 / expected));
  } else if (up.status == UPLOAD_FILE_END) {
    if (updateError.isEmpty()) {
      if (Update.end(true)) { updateOk = true; Diag::willInstall(); }
      else updateError = "Update unvollständig oder beschädigt";
    }
    Net::otaRunning = false;
  } else if (up.status == UPLOAD_FILE_ABORTED) {
    Update.abort(); updateError = "Upload abgebrochen"; Net::otaRunning = false;
  }
}

inline void handleNotFound() {
  if (Net::state == Net::SETUP_AP) {
    // Captive Portal: Handy öffnet die Einrichtungsseite von selbst
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
    return;
  }
  server.send(404, "text/plain", "Nicht gefunden");
}

inline void begin() {
  const char *headers[] = {"Cookie", "Content-Length", "If-None-Match"};
  server.collectHeaders(headers, 3);
  server.on("/", HTTP_GET, handleRoot);
  server.on("/ap.js", HTTP_GET, handleAirportsJs);
  server.on("/api/state", HTTP_GET, handleState);
  server.on("/api/login", HTTP_POST, handleLogin);
  server.on("/api/logout", HTTP_POST, handleLogout);
  server.on("/api/account", HTTP_POST, handleAccount);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/frame", HTTP_GET, handleFrame);
  server.on("/api/settings", HTTP_POST, handleSettings);
  server.on("/api/message", HTTP_POST, handleMessage);
  server.on("/api/security", HTTP_POST, handleSecurity);
  server.on("/api/power", HTTP_POST, handlePower);
  server.on("/api/profile", HTTP_GET, handleProfileGet);
  server.on("/api/profile", HTTP_POST, handleProfileSave);
  server.on("/api/dots", HTTP_POST, handleDots);
  server.on("/api/iss", HTTP_GET, handleIssGet);
  server.on("/api/weather", HTTP_GET, handleWeatherGet);
  server.on("/api/extras", HTTP_GET, handleExtrasGet);
  server.on("/api/guest", HTTP_GET, handleGuestGet);
  server.on("/api/guest", HTTP_POST, handleGuestJoin);
  server.on("/api/extras", HTTP_POST, handleExtrasSave);
  server.on("/api/timer", HTTP_POST, handleTimer);
  server.on("/api/track", HTTP_GET, handleTrack);
  server.on("/api/track", HTTP_POST, handleTrack);
  server.on("/api/badges", HTTP_GET, handleBadges);
  server.on("/api/badges", HTTP_POST, handleBadges);
  server.on("/api/alerts", HTTP_GET, handleAlerts);
  server.on("/api/alerts", HTTP_POST, handleAlerts);
  server.on("/api/song", HTTP_POST, handleSong);
  server.on("/api/album", HTTP_GET, handleAlbum);
  server.on("/api/album", HTTP_POST, handleAlbum);
  server.on("/api/stats", HTTP_GET, handleStats);
  server.on("/api/stats", HTTP_POST, handleStats);
  server.on("/api/backup", HTTP_GET, handleBackup);
  server.on("/api/restore", HTTP_POST, handleRestore);
  server.on("/api/weather", HTTP_POST, handleWeatherSave);
  server.on("/api/game", HTTP_GET, handleGameGet);
  server.on("/api/game", HTTP_POST, handleGamePost);
  server.on("/api/iss", HTTP_POST, handleIssSave);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/api/wifi", HTTP_POST, handleWifi);
  server.on("/api/reboot", HTTP_POST, handleReboot);
  server.on("/api/factory", HTTP_POST, handleFactory);
  server.on("/api/test", HTTP_POST, handleTest);
  server.on("/api/keys", HTTP_GET, handleKeys);
  server.on("/api/alarms", HTTP_GET, handleAlarmsGet);
  server.on("/api/alarms", HTTP_POST, handleAlarmsSave);
  server.on("/api/alarm", HTTP_POST, handleAlarmAction);
  server.on("/api/time", HTTP_POST, handleTime);
  server.on("/api/fw/settings", HTTP_POST, handleFwSettings);
  server.on("/api/flight", HTTP_GET, handleFlightGet);
  server.on("/api/flight", HTTP_POST, handleFlightSave);
  server.on("/api/fw/check", HTTP_POST, handleFwCheck);
  server.on("/api/fw/install", HTTP_POST, handleFwInstall);
  server.on("/api/fw/progress", HTTP_GET, handleFwProgress);
  Updater::pump = [] { server.handleClient(); };
  server.on("/api/update", HTTP_POST, handleUpdateDone, handleUpdateUpload);
  server.onNotFound(handleNotFound);
  server.begin();
  Ws::begin();
}

inline void update() {
  Diag::mainStage(Diag::ST_WEB);
  server.handleClient();
  Ws::update();
  if (guestToken.length() && !Game::active()) guestToken = "";      // Gast-Zugang endet mit dem Spiel
  Diag::mainStage(Diag::ST_IDLE);
  if (rebootAt && (int32_t)(millis() - rebootAt) > 0) { delay(100); ESP.restart(); }
}

}  // namespace Web
