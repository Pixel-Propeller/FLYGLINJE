#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <time.h>
#include "Config.h"
#include "Settings.h"

// ---------------------------------------------------------------------------
// WLAN: verbindet sich mit dem Heimnetz. Ohne Zugangsdaten (oder wenn das
// Heimnetz 60 s nicht erreichbar ist) öffnet die Uhr ein eigenes Einrichtungs-
// WLAN "FLYGLINJE-xxxx". Dessen Passwort ist je Gerät zufällig und läuft als Text
// über die Anzeige (steht auch im Webinterface unter System).
//
// Stromsparen: WLAN-Modem schläft zwischen den Beacons des Routers (Modem-Sleep),
// der Prozessor läuft mit 160 statt 240 MHz. Das spart einen guten Teil des
// ESP-Stroms; das Webinterface antwortet dadurch ein paar Millisekunden später.
// ---------------------------------------------------------------------------
namespace Net {

enum State { CONNECTING, ONLINE, SETUP_AP };
inline State state = CONNECTING;
inline DNSServer dns;
inline String apName;
inline uint32_t connectStart = 0;
inline uint32_t lastRetry = 0;
inline bool otaStarted = false;
inline bool mdnsStarted = false;
inline bool otaRunning = false;
inline int otaPercent = 0;
inline uint32_t apCloseAt = 0;
inline bool justConnected = false;   // App zeigt danach einmal die IP an
inline bool announceIp = false;      // nur nach dem Einrichten eines (neuen) WLANs
inline bool wasConnected = false;

inline String chipSuffix() {
  uint64_t mac = ESP.getEfuseMac();
  char b[5];
  snprintf(b, sizeof(b), "%04X", (uint16_t)(mac >> 32));
  return String(b);
}

inline void startSetupAp() {
  apName = "FLYGLINJE-" + chipSuffix();
  WiFi.mode(WIFI_AP_STA);              // STA-Teil wird für die Netzwerksuche gebraucht
  WiFi.softAP(apName.c_str(), Settings::d.apPass.c_str());
  delay(100);
  dns.start(53, "*", WiFi.softAPIP());   // jede Adresse führt zur Uhr (Einrichtungsseite)
  state = SETUP_AP;
}

// Updates direkt aus der Arduino IDE (nur wenn unter System eingeschaltet).
// Das IDE-Protokoll nutzt nur MD5 – deshalb ab Werk aus.
inline void startOta() {
  if (otaStarted || !Settings::d.ideOta) return;
  ArduinoOTA.setHostname(Settings::d.hostname.c_str());
  ArduinoOTA.setPasswordHash(Settings::d.otaHash.c_str());
  ArduinoOTA.setMdnsEnabled(false);      // mDNS richten wir selbst ein
  ArduinoOTA.onStart([] { otaRunning = true; otaPercent = 0; });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) { otaPercent = t ? (p * 100 / t) : 0; });
  ArduinoOTA.onEnd([] { otaPercent = 100; });
  ArduinoOTA.onError([](ota_error_t) { otaRunning = false; });
  ArduinoOTA.begin();
  if (mdnsStarted) MDNS.enableArduino(3232, true);
  otaStarted = true;
}

inline void stopOta() {
  if (!otaStarted) return;
  ArduinoOTA.end();
  if (mdnsStarted) MDNS.disableArduino();
  otaStarted = false;
}

inline void setIdeOta(bool on) {
  Settings::saveIdeOta(on);
  if (!on) stopOta();
  else if (WiFi.status() == WL_CONNECTED) startOta();
}

inline void applyPower() {
  bool ps = Settings::d.powerSave;
  setCpuFrequencyMhz(ps ? 160 : 240);      // 160 statt 80 MHz: TLS-Aufbau sonst sehr langsam
  WiFi.setSleep(ps ? WIFI_PS_MIN_MODEM : WIFI_PS_NONE);
  // etwas weniger Sendeleistung = kleinere Stromspitzen (hilft bei knapper Versorgung)
  WiFi.setTxPower(ps ? WIFI_POWER_15dBm : WIFI_POWER_19_5dBm);
}

inline void startMdns() {
  if (mdnsStarted) return;
  if (MDNS.begin(Settings::d.hostname.c_str())) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
    // Damit die Arduino IDE die Uhr als Netzwerk-Port anzeigt (nur wenn erlaubt)
    if (Settings::d.ideOta) MDNS.enableArduino(3232, true);
  }
}

inline void begin() {
  WiFi.persistent(false);              // Zugangsdaten nicht zusätzlich im Klartext vom WLAN-Treiber speichern lassen
  WiFi.setHostname(Settings::d.hostname.c_str());
  configTzTime(Settings::d.tz.c_str(), NTP_SERVER_1, NTP_SERVER_2);
  if (Settings::d.wifiSsid.isEmpty()) {
    startSetupAp();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.begin(Settings::d.wifiSsid.c_str(), Settings::d.wifiPass.c_str());
    state = CONNECTING;
    connectStart = millis();
  }
  applyPower();
}

inline void update() {
  if (state == SETUP_AP) dns.processNextRequest();

  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected) { startMdns(); startOta(); }
  if (connected && !wasConnected && announceIp) { justConnected = true; announceIp = false; }
  wasConnected = connected;
  if (connected && state == CONNECTING) state = ONLINE;
  if (connected && state == SETUP_AP) {
    // Einrichtungs-WLAN noch 20 s offen lassen, damit das Handy die Erfolgsmeldung bekommt
    if (apCloseAt == 0) apCloseAt = millis() + 20000;
    if ((int32_t)(millis() - apCloseAt) > 0) {
      dns.stop(); WiFi.softAPdisconnect(true); WiFi.mode(WIFI_STA);
      apCloseAt = 0; state = ONLINE;
      applyPower();                      // Modem-Sleep geht nur ohne eigenes WLAN
    }
  }
  if (!connected && state == ONLINE) {
    state = CONNECTING; connectStart = millis();
  }
  if (state == CONNECTING && millis() - connectStart > 60000) {
    startSetupAp();                      // Heimnetz weg: Einrichtung anbieten, im Hintergrund weiter versuchen
    lastRetry = millis();
  }
  if (state == SETUP_AP && Settings::d.wifiSsid.length() && millis() - lastRetry > 30000) {
    lastRetry = millis();
    if (WiFi.status() != WL_CONNECTED) WiFi.begin(Settings::d.wifiSsid.c_str(), Settings::d.wifiPass.c_str());
  }
  if (otaStarted) ArduinoOTA.handle();
}

// Neue Zugangsdaten testen (blockiert bis zu 15 s) und bei Erfolg speichern
inline bool tryConnect(const String &ssid, const String &pass) {
  WiFi.mode(state == SETUP_AP ? WIFI_AP_STA : WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t = millis();
  while (millis() - t < 15000) {
    if (WiFi.status() == WL_CONNECTED) { Settings::saveWifi(ssid, pass); announceIp = true; return true; }
    if (state == SETUP_AP) dns.processNextRequest();
    delay(100);
  }
  // zurück zum alten Netz
  if (Settings::d.wifiSsid.length()) WiFi.begin(Settings::d.wifiSsid.c_str(), Settings::d.wifiPass.c_str());
  return false;
}

inline bool timeValid() {
  time_t now = time(nullptr);
  return now > 1700000000;   // nach Nov. 2023 = Zeit wurde gesetzt
}

inline String ip() {
  if (state == ONLINE) return WiFi.localIP().toString();
  if (state == SETUP_AP) return WiFi.softAPIP().toString();
  return "";
}

}  // namespace Net
