#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// FLYGLINJE – Grundeinstellungen
// ---------------------------------------------------------------------------
#define FW_NAME     "FLYGLINJE"
#define FW_VERSION  "0.8.5"
#define FW_MODEL    "FL-0340"

// Netzwerk
#define DEFAULT_HOSTNAME "flyglinje"            // erreichbar als http://flyglinje.local
// Passwort des Einrichtungs-WLANs: zufällig je Gerät (läuft über die Anzeige, siehe Settings.h)
#define DEFAULT_TZ       "CET-1CEST,M3.5.0,M10.5.0/3"   // Deutschland, mit Sommerzeit
#define NTP_SERVER_1     "pool.ntp.org"
#define NTP_SERVER_2     "time.cloudflare.com"

// Update-Datei auf deiner Webseite (kann auch später im Webinterface unter System eingetragen werden)
#define DEFAULT_UPDATE_URL "https://bastelbyra.de/firmware/update.php"

// Zugang ab Werk
#define DEFAULT_USER "admin"
#define DEFAULT_PASS "admin"

// ---------------------------------------------------------------------------
// Pinbelegung (siehe Verdrahtungsplan)
// ---------------------------------------------------------------------------
namespace Pin {
// Display-Treiber VK1640: U1 = Chip 0, U2 = Chip 1, U4 = Chip 2
constexpr uint8_t CLK[3]  = {18, 21, 23};   // R1, R4, R77
constexpr uint8_t DATA[3] = {19, 22, 25};   // R37, R12, R78
constexpr uint8_t KEYS_TOP  = 34;           // TOP-KEY Rot, nur angezapft (Schlummern, Wecker/Ton, Helligkeit, Modus)
constexpr uint8_t KEYS_BACK = 35;           // POWER Schwarz, nur angezapft (Einstellen, Wecker, Plus, Minus)
constexpr uint8_t BUZZER    = 26;           // Summer-Signal bei R31 (nicht der 5-polige B-Stecker)
}
