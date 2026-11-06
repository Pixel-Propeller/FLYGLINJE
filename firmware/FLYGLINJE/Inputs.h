#pragma once
#include <Arduino.h>
#include "Config.h"
#include "Settings.h"

// ---------------------------------------------------------------------------
// Tasten (je 4 Tasten an einer Analogleitung, Spannungsteiler) und Summer
// ---------------------------------------------------------------------------
namespace Keys {

enum Key : uint8_t {
  NONE = 0,
  SNOOZE, BELL, BRIGHTNESS, MODE,     // oben  (TOP-KEY)
  SET, ALARM, PLUS, MINUS              // hinten (POWER)
};

inline const char *name(Key k) {
  switch (k) {
    case SNOOZE: return "Schlummern";
    case BELL: return "Wecker/Ton";
    case BRIGHTNESS: return "Helligkeit";
    case MODE: return "Modus";
    case SET: return "Einstellen";
    case ALARM: return "Wecker";
    case PLUS: return "Plus";
    case MINUS: return "Minus";
    default: return "-";
  }
}

// Ungefähre Spannung je Taste in mV (Messwerte aus kobebeefg/ikea-clock-lab).
// Über die Testseite lassen sich die eigenen Werte ablesen.
struct Level { Key key; int mv; };
constexpr Level TOP[]  = {{SNOOZE, 0}, {BELL, 1650}, {BRIGHTNESS, 2190}, {MODE, 2470}};
constexpr Level BACK[] = {{SET, 0},    {ALARM, 1630}, {PLUS, 2150},     {MINUS, 2450}};
constexpr int TOLERANCE = 160;
constexpr int IDLE_ABOVE = 2800;

inline int mvTop = 3300, mvBack = 3300;

inline Key decode(const Level *table, int mv) {
  if (mv >= IDLE_ABOVE) return NONE;
  for (int i = 0; i < 4; i++) if (abs(mv - table[i].mv) <= TOLERANCE) return table[i].key;
  return NONE;
}

struct Channel {
  const Level *table;
  uint8_t pin;
  Key candidate = NONE, held = NONE;
  uint32_t since = 0, pressedAt = 0, repeatAt = 0;
  bool longFired = false, veryLongFired = false;
  bool connected = false;   // erst nach 2 s stabiler Ruhespannung: verhindert Phantom-Tasten bei offenen Eingängen
  uint32_t idleSince = 0;
};

inline Channel ch[2] = {{TOP, Pin::KEYS_TOP}, {BACK, Pin::KEYS_BACK}};
inline uint32_t lastSample = 0;

// Ereignisse für die App
inline Key pressed = NONE;     // kurz gedrückt (beim Loslassen)
inline Key longPressed = NONE; // länger als 1 s gehalten
inline Key veryLong = NONE;    // länger als 5 s gehalten (geheime Nachricht)
inline Key repeated = NONE;    // Plus/Minus gehalten: wiederholt alle 150 ms
inline Key down = NONE;        // aktuell gedrückt (für die Testseite)

inline void begin() {
  analogReadResolution(12);
  for (auto &c : ch) { pinMode(c.pin, INPUT); analogSetPinAttenuation(c.pin, ADC_11db); }
}

inline int readMv(uint8_t pin) { return (int)analogReadMilliVolts(pin); }

inline void update() {
  pressed = NONE; longPressed = NONE; repeated = NONE; veryLong = NONE;
  uint32_t now = millis();
  if (now - lastSample < 15) return;
  lastSample = now;
  mvTop = readMv(Pin::KEYS_TOP);
  mvBack = readMv(Pin::KEYS_BACK);
  down = NONE;
  for (int i = 0; i < 2; i++) {
    Channel &c = ch[i];
    int mv = i == 0 ? mvTop : mvBack;
    if (!c.connected) {
      if (mv >= IDLE_ABOVE) {
        if (!c.idleSince) c.idleSince = now;
        if (now - c.idleSince > 2000) {
          c.connected = true;
          if (!Settings::d.keysSeen) Settings::saveKeysSeen();
        }
      } else c.idleSince = 0;
      continue;
    }
    Key k = decode(c.table, mv);
    if (k != c.candidate) { c.candidate = k; c.since = now; }
    if (now - c.since < 60) continue;                   // entprellen
    if (c.candidate != c.held) {
      if (c.held != NONE && !c.longFired) pressed = c.held;
      c.held = c.candidate; c.pressedAt = now; c.longFired = false; c.veryLongFired = false;
    } else if ((c.held == PLUS || c.held == MINUS) && now - c.pressedAt > 600) {
      if (now - c.repeatAt >= 150) { repeated = c.held; c.repeatAt = now; c.longFired = true; }
    } else if (c.held != NONE && c.held != PLUS && c.held != MINUS && !c.longFired && now - c.pressedAt > 1000) {
      longPressed = c.held; c.longFired = true;
    }
    if (c.held != NONE && c.longFired && !c.veryLongFired && now - c.pressedAt > 5000) { veryLong = c.held; c.veryLongFired = true; }
    if (c.held != NONE) down = c.held;
  }
}

// Beim Einschalten: wird "Schlummern" gehalten?
// (nur wenn die Tasten schon einmal erkannt wurden – ein offener Eingang liest sonst zufällig 0 V)
inline bool snoozeHeldAtBoot() {
  if (!Settings::d.keysSeen) return false;
  int mv = readMv(Pin::KEYS_TOP);
  return mv < TOLERANCE;
}
inline bool connected(int i) { return ch[i].connected; }

}  // namespace Keys

namespace Buzzer {
inline uint32_t stopAt = 0;
inline void begin() { pinMode(Pin::BUZZER, OUTPUT); digitalWrite(Pin::BUZZER, LOW); }
inline void beep(uint16_t hz = 2000, uint16_t ms = 120, bool force = false) {
  if (!Settings::d.sound && !force) return;
  tone(Pin::BUZZER, hz, ms);
}
inline void click() { beep(3000, 15); }
}  // namespace Buzzer
