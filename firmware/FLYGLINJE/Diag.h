#pragma once
#include <Arduino.h>
#include "esp_attr.h"
#include "esp_system.h"
#include "Settings.h"
#include "Config.h"
#include "esp_ota_ops.h"

// ---------------------------------------------------------------------------
// Fehlersuche ohne USB-Kabel:
//  * "Brotkrumen": Die Firmware merkt sich in einem Speicher, der einen Absturz
//    übersteht (RTC), woran sie gerade arbeitet. Nach einem Absturz steht so im
//    Webinterface, WO es passiert ist (z. B. "Flugdaten: Verbindung aufbauen").
//  * Die letzten 6 Neustarts werden mit Grund und Laufzeit gespeichert.
//  * Wurde ein Update automatisch zurückgenommen, wird das angezeigt.
// ---------------------------------------------------------------------------
namespace Diag {

enum Stage : uint8_t {
  ST_IDLE, ST_FL_CONNECT, ST_FL_REQUEST, ST_FL_HEADER, ST_FL_BODY, ST_FL_INFLATE, ST_FL_PARSE,
  ST_FL_ROUTE, ST_WEB, ST_UPD_CHECK, ST_UPD_INSTALL, ST_SAVE, ST_ISS_TLE, ST_ISS_CALC, ST_COUNT
};
inline const char *STAGE_TEXT[ST_COUNT] = {
  "Leerlauf", "Flugdaten: Verbindung aufbauen", "Flugdaten: Anfrage senden", "Flugdaten: Antwort-Kopf",
  "Flugdaten: Daten empfangen", "Flugdaten: Entpacken", "Flugdaten: Auswerten", "Flugdaten: Route nachschlagen",
  "Webinterface", "Update prüfen", "Update installieren", "Einstellungen speichern",
  "ISS: Bahndaten laden", "ISS: Überflüge berechnen"};

// überlebt Software-Neustarts, Abstürze und Watchdog (nicht aber Stromausfall/Unterspannung)
static RTC_NOINIT_ATTR uint32_t magic;
static RTC_NOINIT_ATTR uint8_t stageFlight, stageMain;
static RTC_NOINIT_ATTR uint32_t uptimeS;

constexpr uint32_t MAGIC = 0xF17E0A01;

inline void flight(Stage s) { stageFlight = s; }
inline void mainStage(Stage s) { stageMain = s; }

inline String reasonText(esp_reset_reason_t r) {
  switch (r) {
    case ESP_RST_POWERON:  return "Strom eingeschaltet";
    case ESP_RST_SW:       return "Neustart (gewollt)";
    case ESP_RST_PANIC:    return "Absturz";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:      return "Hängengeblieben (Watchdog)";
    case ESP_RST_BROWNOUT: return "Unterspannung";
    case ESP_RST_EXT:      return "Reset-Taste";
    default:               return "unbekannt";
  }
}

inline String lastText;     // Beschreibung des letzten Neustarts
inline String rollbackText; // gesetzt, wenn ein Update zurückgenommen wurde

inline String fmtUptime(uint32_t s) {
  if (s < 120) return String(s) + " s";
  if (s < 7200) return String(s / 60) + " min";
  return String(s / 3600) + " h";
}

// Beim Start aufrufen (nach Settings::begin)
inline void begin(const char *fwVersion) {
  esp_reset_reason_t r = esp_reset_reason();
  bool valid = magic == MAGIC;
  String t = reasonText(r);
  bool crash = r == ESP_RST_PANIC || r == ESP_RST_INT_WDT || r == ESP_RST_TASK_WDT || r == ESP_RST_WDT;
  if (valid && r != ESP_RST_POWERON && r != ESP_RST_BROWNOUT) t += " nach " + fmtUptime(uptimeS);
  if (valid && crash) {
    if (stageFlight != ST_IDLE && stageFlight < ST_COUNT) t += " · Hintergrund: " + String(STAGE_TEXT[stageFlight]);
    if (stageMain != ST_IDLE && stageMain < ST_COUNT) t += " · Vordergrund: " + String(STAGE_TEXT[stageMain]);
  }
  lastText = t;
  // Verlauf (neueste zuerst, max. 6 Einträge, getrennt durch '\n')
  String hist = Settings::prefs.getString("rlog", "");
  hist = String(fwVersion) + ": " + t + (hist.length() ? "\n" + hist : "");
  int cut = -1, n = 0;
  for (int i = 0; i < (int)hist.length(); i++) if (hist[i] == '\n' && ++n == 6) { cut = i; break; }
  if (cut > 0) hist = hist.substring(0, cut);
  Settings::prefs.putString("rlog", hist);

  // Update zurückgenommen? Vor dem Installieren wird die alte Version gemerkt. Läuft danach
  // wieder genau diese (und keine frisch installierte, unbestätigte), hat der ESP zurückgeschaltet.
  String from = Settings::prefs.getString("tryfrom", "");
  if (from.length()) {
    esp_ota_img_states_t st;
    bool pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
    if (!pending) {   // (frisch installiert & unbestätigt: Merker behalten, bis die Version bestätigt ist)
      if (from == fwVersion)
        rollbackText = "Das letzte Update wurde zurückgenommen: die neue Version ist in den ersten 30 Sekunden neu gestartet (" + t + "). Es läuft wieder " + String(fwVersion) + ".";
      Settings::prefs.remove("tryfrom");
    }
  }

  magic = MAGIC; stageFlight = ST_IDLE; stageMain = ST_IDLE; uptimeS = 0;
}

inline void tick() { uptimeS = millis() / 1000; }

// Hänger der Hauptschleife messen (Spiele ruckeln sonst ohne erkennbaren Grund):
// längster Aufruf über 150 ms der letzten 2 Minuten, mit Ort
inline uint32_t stallMs = 0, stallAt = 0;
inline const char *stallWhere = "";
inline void stall(const char *where, uint32_t ms) {
  if (ms > stallMs || millis() - stallAt > 120000) { stallMs = ms; stallWhere = where; stallAt = millis(); }
  Serial.printf("Hänger: %s %lu ms\n", where, (unsigned long)ms);
}
inline uint32_t stallRecent() { return stallMs && millis() - stallAt <= 120000 ? stallMs : 0; }
inline void confirmed() { Settings::prefs.remove("tryfrom"); }
inline void willInstall() { Settings::prefs.putString("tryfrom", FW_VERSION); }
inline String history() { return Settings::prefs.getString("rlog", ""); }

}  // namespace Diag
