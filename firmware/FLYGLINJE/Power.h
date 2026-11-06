#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Display.h"
#include "Settings.h"
#include "Net.h"

// ---------------------------------------------------------------------------
// Stromverbrauch fürs Webinterface.
//  * Ohne Messgerät: Schätzung aus Prozessor-/WLAN-Modus und den gerade
//    leuchtenden LEDs (Anzahl × Helligkeit). Grob, etwa ±30 %.
//  * Mit INA219-Modul (I²C, SDA = GPIO32, SCL = GPIO33, 0,1-Ω-Shunt in der
//    5-V-Leitung): echter Messwert. Wird beim Start automatisch erkannt.
// ---------------------------------------------------------------------------
namespace Power {

constexpr uint8_t INA_ADDR = 0x40;
inline bool ina = false;

inline void begin() {
  Wire.begin(32, 33);
  Wire.setTimeOut(20);
  Wire.beginTransmission(INA_ADDR);
  ina = Wire.endTransmission() == 0;
}

inline bool readReg(uint8_t reg, int16_t &v) {
  Wire.beginTransmission(INA_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)INA_ADDR, 2) != 2) return false;
  v = (int16_t)((Wire.read() << 8) | Wire.read());
  return true;
}

// Messwert vom INA219: Strom (mA) und Spannung (V)
inline bool measure(float &mA, float &volt) {
  if (!ina) return false;
  int16_t shunt, bus;
  if (!readReg(0x01, shunt) || !readReg(0x02, bus)) return false;
  mA = shunt * 0.1f;                                 // 10 µV je Schritt an 0,1 Ω
  volt = ((uint16_t)bus >> 3) * 0.004f;               // 4 mV je Schritt
  return true;
}

// Leuchtende LEDs (aus dem zuletzt übertragenen Treiberspeicher)
inline int litLeds() {
  if (!Display::enabled) return 0;
  int n = 0;
  for (int c = 0; c < Display::CHIPS; c++)
    for (int i = 0; i < Display::RAM; i++) n += __builtin_popcount(Display::sent[c][i]);
  return n;
}

// VK1640/TM1640: 16 Gitter im Wechsel, Pulsbreite je Helligkeitsstufe (x/16)
constexpr float SEG_MA = 15.0f;                       // Strom einer LED, während sie an ist
inline int duty() {
  static const uint8_t DUTY[8] = {1, 2, 4, 10, 11, 12, 13, 14};
  return Display::enabled ? DUTY[Display::brightness & 7] : 0;
}

// Schätzung in mA (5-V-Seite)
inline void estimate(int &espMa, int &ledMa, int &lit) {
  bool ap = Net::state == Net::SETUP_AP;
  espMa = (!Settings::d.powerSave || ap) ? 115 : 45;
  // VK1640/TM1640: 16 Gitter im Wechsel, Pulsbreite je Helligkeitsstufe (x/16)
  lit = litLeds();
  ledMa = (int)lroundf(lit * SEG_MA * duty() / 16.0f / 16.0f);
}

}  // namespace Power
