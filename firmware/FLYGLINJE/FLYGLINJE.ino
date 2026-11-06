// ===========================================================================
//  FLYGLINJE – Firmware für den umgebauten IKEA KORTLINJE
//  Uhr, zwei Wecker, Webinterface, WLAN-Einrichtung, Updates per WLAN (OTA) –
//  auch automatisch von der eigenen Webseite – und Hardware-Test.
//
//  Arduino IDE:
//    Board:            ESP32 Dev Module   (Boardpaket "esp32" von Espressif, 3.x)
//    Partition Scheme: Minimal SPIFFS (1.9MB APP with OTA/190KB SPIFFS)
//    Keine zusätzlichen Bibliotheken nötig.
//
//  Erster Start: Die Uhr öffnet das WLAN "FLYGLINJE-xxxx". Das Passwort ist je
//  Gerät zufällig und läuft über die Anzeige (steht auch im seriellen Monitor).
//  Damit verbinden, die Einrichtungsseite öffnet sich (sonst http://192.168.4.1).
// ===========================================================================

#include "Config.h"
#include "Settings.h"
#include "Diag.h"
#include "Display.h"
#include "Inputs.h"
#include "Net.h"
#include "App.h"
#include "Alarm.h"
#include "Updater.h"
#include "Flight.h"
#include "Iss.h"
#include "Weather.h"
#include "Badges.h"
#include "Tracker.h"
#include "Alerts.h"
#include "Album.h"
#include "Power.h"
#include "Web.h"
#include "esp_ota_ops.h"

// Rollback-Schutz: eine neu aufgespielte Firmware gilt erst als gut, wenn sie
// 30 s ohne Absturz läuft. Stürzt sie vorher ab, startet der ESP die alte Version.
extern "C" bool verifyRollbackLater() { return true; }
static bool appConfirmed = false;

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println(F(FW_NAME " " FW_VERSION));

  Settings::begin();
  Diag::begin(FW_VERSION);
  Serial.println("Letzter Start: " + Diag::lastText);
  Serial.printf("Einrichtungs-WLAN: FLYGLINJE-%s  Passwort: %s\n", Net::chipSuffix().c_str(), Settings::d.apPass.c_str());
  Buzzer::begin();
  Keys::begin();
  Display::rotate180 = Settings::d.rotate180;
  Display::setBrightness(Settings::d.brightness);
  Display::begin();

  // "Schlummern" beim Einschalten gedrückt: 10-s-Countdown zum Zurücksetzen des Zugangs
  if (Keys::snoozeHeldAtBoot()) {
    App::resetCountdown = true;
    App::resetStart = millis();
  }

  Alarm::begin();
  Iss::begin();
  Weather::begin();
  Extras::load();
  Stats::load();
  Badges::load();
  Tracker::begin();
  Alerts::load();
  Album::begin();
  // ISS und Wetter laufen im Hintergrund-Task des Flugmodus mit (gleicher Standort)
  Flight::extraService = [](double lat, double lon) {
    Iss::service(lat, lon);
    Tracker::service(lat, lon);                      // verfolgter Flug: auch nachts (Landung melden)
    Album::flush();                                  // Sammelalbum gesammelt speichern
    if (!Flight::paused) { Weather::service(lat, lon); Alerts::service(lat, lon); }
  };
  Flight::begin();
  Profile::begin();                      // Tag-/Nachtprofil (übernimmt Flug-, ISS- und Anzeige-Werte)
  Power::begin();
  Game::begin();
  App::begin();
  Net::begin();
  Web::begin();
}

// Jeden Schritt der Schleife messen: dauert einer länger als 150 ms, merkt sich Diag das
#define TIMED(name, call) do { uint32_t t0_ = millis(); call; uint32_t d_ = millis() - t0_; if (d_ > 150) Diag::stall(name, d_); } while (0)

void loop() {
  TIMED("WLAN", Net::update());
  TIMED("Webinterface", Web::update());
  Keys::update();
  Alarm::update();
  TIMED("Anzeige", App::update());
  TIMED("Matrix", Display::update());
  if (!Game::active()) TIMED("Update-Suche", Updater::update());   // nicht mitten im Spiel
  Diag::tick();

  if (!appConfirmed && millis() > 30000) {
    esp_ota_mark_app_valid_cancel_rollback();
    Diag::confirmed();
    appConfirmed = true;
  }
  delay(4);   // CPU schläft dazwischen; reicht für Tasten, Laufschrift und Summer
}
