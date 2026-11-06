#pragma once
#include <Arduino.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "Settings.h"
#include "Net.h"

// ---------------------------------------------------------------------------
// Abzeichen: kleine Erfolge, die die Uhr nebenbei sammelt (A380 gesehen,
// 100 Flüge an einem Tag, Frühaufsteher …). earn() darf aus jedem Task gerufen
// werden; die Anzeige („ABZEICHEN: …“) übernimmt die Hauptschleife.
// ---------------------------------------------------------------------------
namespace Badges {

struct Def { const char *name; const char *hint; };
// Reihenfolge = Bit-Nummer, nie umsortieren (gespeichert als Bitmaske)
inline const Def DEFS[] = {
  {"ERSTER FLUG", "Das erste Flugzeug im Radius"},
  {"A380", "Einen Airbus A380 gesehen"},
  {"JUMBO", "Eine Boeing 747 gesehen"},
  {"BELUGA", "Den Airbus Beluga gesehen"},
  {"MILITÄR", "Ein Militärflugzeug gesehen"},
  {"HUBSCHRAUBER", "Einen Hubschrauber gesehen"},
  {"RETTUNGSFLUG", "Einen Rettungsflug gesehen"},
  {"NOTFALL", "Ein Flugzeug mit Notfall-Code mitbekommen"},
  {"ÜBER DIR", "Ein Flugzeug fast senkrecht über dem Haus"},
  {"100 AM TAG", "100 Flugzeuge an einem Tag"},
  {"1000 FLÜGE", "1.000 Flugzeuge insgesamt"},
  {"10000 FLÜGE", "10.000 Flugzeuge insgesamt"},
  {"HÖHENFLUG", "Ein Flug über 43.000 Fuß"},
  {"TIEFFLIEGER", "Ein Flugzeug näher als 1 km"},
  {"ISS GEWUNKEN", "Einen ISS-Überflug erlebt"},
  {"FRÜHAUFSTEHER", "Vor 6 Uhr aufgestanden, ohne zu schlummern"},
  {"SCHLAFMÜTZE", "Dreimal hintereinander geschlummert"},
  {"SNAKE-PROFI", "20 Punkte bei Snake"},
  {"PONG-SIEG", "Pong gegen die Uhr gewonnen"},
  {"DOPPELPASS", "Pong zu zweit gespielt"},
  {"FROSTIG", "0 °C oder kälter"},
  {"HITZEFREI", "30 °C oder wärmer"},
  {"SCHIRMWETTER", "Regenwahrscheinlichkeit 100 %"},
  {"EIERUHR", "Der Küchentimer ist abgelaufen"},
  {"GELANDET", "Einen verfolgten Flug bis zur Landung begleitet"},
  {"GEHEIMNIS", ""},                                  // wird nicht verraten
  {"UNWETTER", "Eine Unwetterwarnung erlebt"},
  {"STÄNDCHEN", "Das Geburtstagsständchen gehört"},
  {"SAMMLER", "10 Flugzeugtypen im Album"},
  {"KENNER", "50 Flugzeugtypen im Album"},
  {"LEGENDÄR", "Eine legendäre Karte im Album"},
};
constexpr int COUNT = sizeof(DEFS) / sizeof(DEFS[0]);
enum Id : uint8_t {
  FIRST, A380, JUMBO, BELUGA, MILITARY, HELI, RESCUE, EMERGENCY, OVERHEAD, DAY100, TOTAL1000, TOTAL10000,
  HIGHFLY, LOWFLY, ISS, EARLY, SLEEPY, SNAKE, PONG, PONG2, FROST, HEAT, RAIN, TIMER, LANDED, SECRET, STORM, SONG,
  TYPES10, TYPES50, LEGEND
};

inline uint32_t have = 0;                // erreicht (gespeichert)
inline uint32_t at[32];                  // wann (Unix-Zeit)
inline volatile uint32_t pending = 0;    // neu, noch nicht angezeigt
inline portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

inline void load() {
  have = Settings::prefs.getUInt("bdg", 0);
  memset(at, 0, sizeof(at));
  if (Settings::prefs.getBytesLength("bdgt") == sizeof(at)) Settings::prefs.getBytes("bdgt", at, sizeof(at));
}

// aus jedem Task: merkt sich das Abzeichen zum Anzeigen vor
inline void earn(uint8_t id) {
  if (id >= COUNT || (have & (1u << id))) return;
  portENTER_CRITICAL(&mux); pending |= 1u << id; portEXIT_CRITICAL(&mux);
}

// Hauptschleife: nächstes neues Abzeichen holen und speichern (-1 = keins)
inline int takeNew() {
  portENTER_CRITICAL(&mux);
  uint32_t p = pending & ~have;
  int id = -1;
  if (p) { id = __builtin_ctz(p); pending &= ~(1u << id); }
  portEXIT_CRITICAL(&mux);
  if (id < 0) return -1;
  have |= 1u << id;
  at[id] = Net::timeValid() ? (uint32_t)time(nullptr) : 0;
  Settings::prefs.putUInt("bdg", have);
  Settings::prefs.putBytes("bdgt", at, sizeof(at));
  return id;
}

inline int countHave() { return __builtin_popcount(have); }

inline String json() {
  String j = "{\"have\":" + String(countHave()) + ",\"total\":" + String(COUNT) + ",\"list\":[";
  for (int i = 0; i < COUNT; i++) {
    if (i) j += ",";
    bool got = have & (1u << i);
    String hint = DEFS[i].hint;
    j += "{\"name\":\"" + String(DEFS[i].name) + "\",\"hint\":\"" + hint + "\",\"got\":" + (got ? "true" : "false") +
         ",\"at\":" + String(got ? at[i] : 0) + "}";
  }
  return j + "]}";
}

inline void reset() { have = 0; memset(at, 0, sizeof(at)); Settings::prefs.putUInt("bdg", 0); Settings::prefs.putBytes("bdgt", at, sizeof(at)); }

}  // namespace Badges
