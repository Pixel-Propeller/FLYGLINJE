#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "Fetch.h"
#include "Diag.h"
#include <Update.h>
#include "mbedtls/sha256.h"
#include "Config.h"
#include "Settings.h"
#include "Display.h"
#include "Net.h"
#include "Alarm.h"

// ---------------------------------------------------------------------------
// Updates von der eigenen Webseite.
// Die Uhr lädt eine kleine JSON-Datei (Update-Adresse in System › Firmware), z. B.
//   {"version":"0.2.0","url":"https://…/flyglinje-0.2.0.bin","size":1234567,"sha256":"…","notes":"…"}
// Ist die Version neuer, kann sie die .bin laden und installieren. Die Prüfsumme
// (SHA-256) wird beim Herunterladen gegengerechnet – passt sie nicht, wird nichts installiert.
// ---------------------------------------------------------------------------

// Zertifikatsliste der gängigen Root-CAs (steckt bereits in der ESP32-Bibliothek)
extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_imported_bundle_bin_end[] asm("_binary_x509_crt_bundle_end");

namespace Updater {

inline String latest, binUrl, sha256, notes, error;
inline uint32_t size = 0;
inline bool available = false;
inline bool installRequested = false;
inline uint32_t lastCheck = 0;
inline bool checkedOnce = false;
inline String lastCheckText = "noch nie";

// ---- Hilfen ------------------------------------------------------------------------
inline String jsonValue(const String &body, const char *key) {
  String k = String("\"") + key + "\"";
  int i = body.indexOf(k);
  if (i < 0) return "";
  i = body.indexOf(':', i + k.length());
  if (i < 0) return "";
  i++;
  while (i < (int)body.length() && isspace((unsigned char)body[i])) i++;
  if (i >= (int)body.length()) return "";
  if (body[i] == '"') {
    String out;
    for (int j = i + 1; j < (int)body.length(); j++) {
      char c = body[j];
      if (c == '\\' && j + 1 < (int)body.length()) {
        char n = body[++j];
        if (n == 'n') out += '\n'; else if (n == '/') out += '/'; else out += n;
      } else if (c == '"') break;
      else out += c;
    }
    return out;
  }
  int e = i;
  while (e < (int)body.length() && body[e] != ',' && body[e] != '}' && !isspace((unsigned char)body[e])) e++;
  return body.substring(i, e);
}

// "v0.10.2" -> vergleichbare Zahl
inline uint32_t versionNumber(String v) {
  v.trim();
  if (v.startsWith("v") || v.startsWith("V")) v = v.substring(1);
  uint32_t parts[3] = {0, 0, 0};
  int p = 0;
  for (size_t i = 0; i < v.length() && p < 3; i++) {
    char c = v[i];
    if (isdigit((unsigned char)c)) parts[p] = parts[p] * 10 + (c - '0');
    else if (c == '.') p++;
    else break;
  }
  return parts[0] * 1000000 + parts[1] * 1000 + parts[2];
}
inline bool isNewer(const String &v) { return versionNumber(v) > versionNumber(FW_VERSION); }

// Client passend zu http/https
struct Connection {
  WiFiClient plain;
  WiFiClientSecure secure;
  bool begin(HTTPClient &http, const String &url) {
    http.setTimeout(15000);
    http.setConnectTimeout(8000);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent(String(FW_NAME "/") + FW_VERSION);
    if (url.startsWith("https://")) {
      secure.setCACertBundle(x509_crt_imported_bundle_bin_start,
                             x509_crt_imported_bundle_bin_end - x509_crt_imported_bundle_bin_start);
      return http.begin(secure, url);
    }
    return http.begin(plain, url);
  }
};

inline String timeText() {
  if (!Net::timeValid()) return "eben";
  time_t now = time(nullptr); struct tm t; localtime_r(&now, &t);
  char b[24]; strftime(b, sizeof(b), "%d.%m. %H:%M", &t);
  return b;
}

// ---- Prüfen ------------------------------------------------------------------------
inline bool check() {
  lastCheck = millis(); checkedOnce = true; error = "";
  String url = Settings::d.updateUrl;
  if (url.isEmpty()) { error = "Keine Update-Adresse eingetragen"; return false; }
  if (WiFi.status() != WL_CONNECTED) { error = "Kein WLAN"; return false; }
  Fetch::TlsGuard guard(30000);
  if (!guard.ok) { error = "Netz gerade belegt – gleich nochmal versuchen"; return false; }
  Diag::mainStage(Diag::ST_UPD_CHECK);
  struct Idle { ~Idle() { Diag::mainStage(Diag::ST_IDLE); } } idle;
  HTTPClient http; Connection con;
  if (!con.begin(http, url)) { error = "Adresse ungültig"; return false; }
  int code = http.GET();
  if (code != 200) {
    error = code > 0 ? "Server antwortet mit " + String(code) : "Server nicht erreichbar (" + http.errorToString(code) + ")";
    http.end(); return false;
  }
  String body = http.getString();
  http.end();
  String v = jsonValue(body, "version");
  String u = jsonValue(body, "url");
  if (v.isEmpty() || u.isEmpty()) { error = "Update-Datei unvollständig (version/url fehlt)"; return false; }
  if (!u.startsWith("http")) {                       // relative Adresse: an die Update-Adresse anhängen
    int slash = url.lastIndexOf('/');
    u = url.substring(0, slash + 1) + u;
  }
  latest = v; binUrl = u;
  sha256 = jsonValue(body, "sha256"); sha256.toLowerCase();
  notes = jsonValue(body, "notes");
  size = jsonValue(body, "size").toInt();
  available = isNewer(v);
  lastCheckText = timeText();
  return true;
}

// ---- Installieren --------------------------------------------------------------------
// Während des Downloads blockiert install() die Hauptschleife. Damit das Webinterface
// trotzdem den Fortschritt abfragen kann, ruft install() regelmäßig pump() auf
// (setzt Web.h: bearbeitet wartende Anfragen).
inline void (*pump)() = nullptr;
inline volatile bool installing = false;
inline volatile bool restarting = false;
inline void drawProgress(int pct) {
  Display::rawMode = false;
  Display::clear();
  Display::drawCentered("UPDATE");
  for (int x = 0; x < pct * Display::W / 100; x++) Display::set(x, 8);
  Display::update();
}

inline bool install() {
  error = "";
  if (!available || binUrl.isEmpty()) { error = "Kein Update verfügbar"; return false; }
  Net::otaRunning = true;                  // Flugmodus hält an
  installing = true;
  struct Done { ~Done() { installing = false; } } done;
  Fetch::TlsGuard guard(60000);
  if (!guard.ok) { error = "Netz belegt – Update abgebrochen"; Net::otaRunning = false; return false; }
  Diag::mainStage(Diag::ST_UPD_INSTALL);
  HTTPClient http; Connection con;
  if (!con.begin(http, binUrl)) { error = "Download-Adresse ungültig"; Net::otaRunning = false; return false; }
  int code = http.GET();
  if (code != 200) { error = "Download fehlgeschlagen (" + String(code) + ")"; http.end(); Net::otaRunning = false; return false; }
  int len = http.getSize();
  if (len <= 0 && size > 0) len = size;
  if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN, U_FLASH)) { error = "Kein Platz für das Update"; http.end(); Net::otaRunning = false; return false; }

  Net::otaRunning = true; Net::otaPercent = 0;   // Flugmodus pausiert solange (mehr RAM und Ruhe im WLAN)
  mbedtls_sha256_context ctx; mbedtls_sha256_init(&ctx); mbedtls_sha256_starts(&ctx, 0);
  WiFiClient *stream = http.getStreamPtr();
  uint8_t buf[2048];
  int total = 0; bool first = true;
  uint32_t lastData = millis(), lastPump = 0;
  // Wichtig: Auch wenn der Server die Verbindung schon geschlossen hat, können noch
  // Daten im Puffer liegen – deshalb "connected() ODER available()" (Fehler in 0.4.0/0.4.1)
  while ((len <= 0 || total < len) && (http.connected() || stream->available())) {
    size_t avail = stream->available();
    if (!avail) {
      if (millis() - lastData > 15000) { error = "Download hängt (keine Daten mehr)"; break; }
      delay(2); continue;
    }
    if (pump && millis() - lastPump > 150) { lastPump = millis(); pump(); }
    int n = stream->readBytes(buf, min(avail, sizeof(buf)));
    if (n <= 0) continue;
    lastData = millis();
    if (first) { first = false; if (buf[0] != 0xE9) { error = "Datei ist keine ESP32-Firmware"; break; } }
    mbedtls_sha256_update(&ctx, buf, n);
    if (Update.write(buf, n) != (size_t)n) { error = "Schreibfehler"; break; }
    total += n;
    if (len > 0) {
      int pct = total * 100 / len;
      if (pct != Net::otaPercent) { Net::otaPercent = pct; drawProgress(pct); }
    }
  }
  http.end();
  uint8_t digest[32]; mbedtls_sha256_finish(&ctx, digest); mbedtls_sha256_free(&ctx);
  String got = Settings::toHex(digest, 32);

  if (error.isEmpty() && len > 0 && total != len) error = "Download unvollständig (" + String(total / 1024) + " von " + String(len / 1024) + " KB)";
  if (error.isEmpty() && sha256.length() && got != sha256) error = "Prüfsumme stimmt nicht – Update verworfen";
  if (error.length()) { Update.abort(); Net::otaRunning = false; return false; }
  if (!Update.end(true)) { error = "Update beschädigt"; Net::otaRunning = false; return false; }
  Diag::willInstall();
  drawProgress(100);
  Net::otaPercent = 100; restarting = true;
  // noch ~1,5 s Anfragen beantworten, damit das Handy „100 %“ sieht
  for (uint32_t t0 = millis(); millis() - t0 < 1500;) { if (pump) pump(); delay(20); }
  ESP.restart();
  return true;
}

// ---- Im loop() --------------------------------------------------------------------------
inline void update() {
  if (installRequested) { installRequested = false; install(); return; }
  if (Settings::d.updateUrl.isEmpty() || Net::state != Net::ONLINE) return;
  uint32_t now = millis();
  // erste Prüfung 1 Minute nach dem Start, dann alle 12 Stunden
  bool due = (!checkedOnce && now > 60000) || (checkedOnce && now - lastCheck > 12UL * 3600UL * 1000UL);
  if (!due) return;
  check();
  // Automatisch installieren: nur nachts (2–5 Uhr) und wenn kein Wecker klingelt
  if (available && Settings::d.autoUpdate && Alarm::state == Alarm::IDLE && Net::timeValid()) {
    time_t t0 = time(nullptr); struct tm t; localtime_r(&t0, &t);
    if (t.tm_hour >= 2 && t.tm_hour < 5) install();
    else lastCheck = now - 11UL * 3600UL * 1000UL;   // in einer Stunde nochmal schauen
  }
}

}  // namespace Updater
