#pragma once
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include "Config.h"
#include "HttpBody.h"
#include "Diag.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

// ---------------------------------------------------------------------------
// Schlanker HTTPS-Abruf für den Flugmodus – spart Daten und Strom:
//  * Antworten gzip-komprimiert anfordern (Flugliste ca. 5× kleiner)
//  * Verbindung offen halten (Keep-Alive): der TLS-Aufbau mit Zertifikaten
//    kostet sonst bei jeder Abfrage mehrere KB und Rechenzeit
//  * Zertifikate werden immer geprüft (eingebautes CA-Bündel)
// ---------------------------------------------------------------------------

extern const uint8_t x509_crt_imported_bundle_bin_start[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t x509_crt_imported_bundle_bin_end[] asm("_binary_x509_crt_bundle_end");

namespace Fetch {

struct Conn {
  WiFiClientSecure *tls = nullptr;
  char host[40] = "";
  uint32_t lastUse = 0;
  uint32_t maxIdleMs = 60000;   // länger offen gehaltene Verbindungen schließen die Server ohnehin
  bool open = false;
  uint32_t requests = 0, handshakes = 0;

  void close() {
    if (tls) { tls->stop(); delete tls; tls = nullptr; }
    open = false;
  }
};

// Immer nur EINE verschlüsselte Verbindung gleichzeitig (Flugdaten oder Update) –
// jede braucht ~40 KB RAM, zwei parallel plus Entpacken wird auf dem ESP32 zu eng.
inline SemaphoreHandle_t tlsLock = nullptr;
inline void initLock() { if (!tlsLock) tlsLock = xSemaphoreCreateMutex(); }
inline bool lockTls(uint32_t ms) { initLock(); return xSemaphoreTake(tlsLock, pdMS_TO_TICKS(ms)) == pdTRUE; }
inline void unlockTls() { xSemaphoreGive(tlsLock); }
struct TlsGuard {
  bool ok;
  explicit TlsGuard(uint32_t ms) : ok(lockTls(ms)) {}
  ~TlsGuard() { if (ok) unlockTls(); }
};

// Statistik für das Webinterface
inline uint32_t bytesWire = 0;     // über die Leitung (Nutzdaten, ohne TLS)
inline uint32_t bytesPlain = 0;    // nach dem Entpacken
inline uint32_t handshakes = 0, reused = 0;
inline int lastRetryAfter = 0;     // Sekunden aus „Retry-After“ der letzten Antwort (0 = keine Angabe)

// Anfragen der letzten Stunde (12 Fächer à 5 Minuten)
inline uint16_t reqBucket[12];
inline uint32_t reqSlot = 0;
inline void advanceSlots() {
  uint32_t slot = millis() / 300000UL;
  if (slot != reqSlot) {
    for (uint32_t k = reqSlot + 1; k <= slot && k <= reqSlot + 12; k++) reqBucket[k % 12] = 0;
    reqSlot = slot;
  }
}
inline void countRequest() { advanceSlots(); reqBucket[reqSlot % 12]++; }
inline int requestsLastHour() {
  advanceSlots();
  int n = 0; for (auto b : reqBucket) n += b; return n;
}

inline bool readLine(WiFiClientSecure &c, char *buf, size_t n, uint32_t timeoutMs) {
  size_t o = 0;
  uint32_t t = millis();
  while (millis() - t < timeoutMs) {
    if (!c.available()) {
      if (!c.connected()) return false;
      vTaskDelay(pdMS_TO_TICKS(5)); continue;
    }
    int ch = c.read();
    if (ch < 0) continue;
    bytesWire++;
    if (ch == '\n') { if (o && buf[o - 1] == '\r') o--; buf[o] = 0; return true; }
    if (o + 1 < n) buf[o++] = (char)ch;
  }
  return false;
}

inline bool startsWithNoCase(const char *s, const char *p) {
  while (*p) { if (tolower((unsigned char)*s++) != tolower((unsigned char)*p++)) return false; }
  return true;
}

// GET https://host/path. Der entpackte Inhalt geht an sink. Rückgabe: HTTP-Status
// oder -1 bei Verbindungs-/Datenfehlern (Text in err).
inline int get(Conn &c, const char *host, const char *path, HttpBody::Sink sink, void *user,
               size_t limit, String &err, bool keepAlive = true, const char *postJson = nullptr) {
  lastRetryAfter = 0;
  TlsGuard guard(30000);
  if (!guard.ok) { err = "Netz gerade belegt (Update?)"; return -1; }
  for (int attempt = 0; attempt < 2; attempt++) {
    bool fresh = false;
    if (c.open && (strcmp(c.host, host) != 0 || !c.tls || !c.tls->connected() || millis() - c.lastUse > c.maxIdleMs)) c.close();
    if (!c.open) {
      // Nur wenn genug Speicher da ist (TLS braucht ~40 KB, gzip zusätzlich ~43 KB)
      if (ESP.getMaxAllocHeap() < 50000) { err = "zu wenig Speicher"; return -1; }
      c.tls = new WiFiClientSecure();
      c.tls->setCACertBundle(x509_crt_imported_bundle_bin_start, x509_crt_imported_bundle_bin_end - x509_crt_imported_bundle_bin_start);
      c.tls->setHandshakeTimeout(12);
      Diag::flight(Diag::ST_FL_CONNECT);
      if (!c.tls->connect(host, 443, 8000)) {
        char e[64] = ""; c.tls->lastError(e, sizeof(e));
        err = String(host) + ": keine Verbindung" + (e[0] ? String(" (") + e + ")" : String(""));
        c.close(); return -1;
      }
      strncpy(c.host, host, sizeof(c.host) - 1); c.host[sizeof(c.host) - 1] = 0;
      c.open = true; fresh = true; handshakes++; c.handshakes++;
    } else {
      reused++;
    }
    WiFiClientSecure &t = *c.tls;
    Diag::flight(Diag::ST_FL_REQUEST);

    String req;
    req.reserve(220);
    req += postJson ? "POST " : "GET "; req += path; req += " HTTP/1.1\r\nHost: "; req += host;
    if (postJson) { req += "\r\nContent-Type: application/json\r\nContent-Length: "; req += String((unsigned)strlen(postJson)); }
    // gzip nur, wenn genug zusammenhängender Speicher fürs Entpacken frei ist (~43 KB)
    bool wantGzip = ESP.getMaxAllocHeap() > 60000;
    req += "\r\nUser-Agent: " FW_NAME "/" FW_VERSION " (bastelbyra.de)\r\nAccept: application/json\r\nAccept-Encoding: ";
    req += wantGzip ? "gzip" : "identity";
    req += "\r\nConnection: ";
    req += keepAlive ? "keep-alive" : "close";
    req += "\r\n\r\n";
    if (postJson) req += postJson;
    countRequest();
    if (t.print(req) != req.length()) {
      c.close();
      if (!fresh) continue;                    // alte Verbindung war schon zu: neu aufbauen
      err = String(host) + ": Senden fehlgeschlagen"; return -1;
    }
    c.requests++;

    char line[160];
    Diag::flight(Diag::ST_FL_HEADER);
    if (!readLine(t, line, sizeof(line), 10000)) {
      c.close();
      if (!fresh) continue;
      err = String(host) + ": keine Antwort"; return -1;
    }
    int status = 0;
    if (strncmp(line, "HTTP/1.", 7) == 0) status = atoi(line + 9);
    if (status < 100) {
      c.close();
      if (!fresh) continue;
      err = String(host) + ": ungültige Antwort"; return -1;
    }

    long length = -1; bool chunked = false, gzip = false, serverClose = false;
    while (true) {
      if (!readLine(t, line, sizeof(line), 10000)) { c.close(); err = String(host) + ": Kopf unvollständig"; return -1; }
      if (!line[0]) break;
      if (startsWithNoCase(line, "content-length:")) length = atol(line + 15);
      else if (startsWithNoCase(line, "transfer-encoding:") && strstr(line, "chunked")) chunked = true;
      else if (startsWithNoCase(line, "content-encoding:") && strstr(line, "gzip")) gzip = true;
      else if (startsWithNoCase(line, "connection:") && (strstr(line, "close") || strstr(line, "Close"))) serverClose = true;
      else if (startsWithNoCase(line, "retry-after:")) { int v = atoi(line + 12); if (v > 0 && v < 86400) lastRetryAfter = v; }
    }

    if (status != 200) {
      // Inhalt nicht auswerten; Verbindung schließen ist am einfachsten
      c.close();
      err = String(host) + ": HTTP " + String(status);
      return status;
    }

    HttpBody::Decoder dec;
    if (!dec.begin(chunked, gzip, sink, user, limit)) { c.close(); err = dec.error; return -1; }
    uint8_t buf[512];
    long left = chunked ? -1 : length;
    uint32_t lastData = millis();
    bool complete = false;
    while (true) {
      if (left == 0 || dec.finished()) { complete = true; break; }
      int avail = t.available();
      if (avail <= 0) {
        if (!t.connected()) { complete = (left < 0 && !chunked); break; }   // Ende = Verbindungsende
        if (millis() - lastData > 8000) break;
        vTaskDelay(pdMS_TO_TICKS(5)); continue;
      }
      int want = (int)sizeof(buf);
      if (left > 0 && left < want) want = (int)left;
      if (avail < want) want = avail;
      int r = t.read(buf, want);
      if (r <= 0) continue;
      lastData = millis();
      bytesWire += r;
      if (left > 0) left -= r;
      Diag::flight(Diag::ST_FL_INFLATE);
      bool fed = dec.feed(buf, r);
      Diag::flight(Diag::ST_FL_BODY);
      if (!fed) break;
    }
    bytesPlain += dec.produced;
    if (dec.failed) { c.close(); err = String(host) + ": " + dec.error; return -1; }
    if (!complete) { c.close(); err = String(host) + ": Antwort unvollständig"; return -1; }
    // Verbindung nur behalten, wenn alles gelesen wurde und der Server zustimmt
    bool canKeep = keepAlive && !serverClose && (chunked || length >= 0) && ESP.getFreeHeap() > 90000 && ESP.getMaxAllocHeap() > 60000;
    if (canKeep) c.lastUse = millis(); else c.close();
    return 200;
  }
  err = String(host) + ": Verbindung verloren";
  return -1;
}

}  // namespace Fetch
