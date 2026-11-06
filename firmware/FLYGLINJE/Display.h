#pragma once
#include <Arduino.h>
#include "Config.h"
#include "Font.h"

// ---------------------------------------------------------------------------
// LED-Matrix 42 x 9 an drei VK1640-Treibern (TM1640-kompatibel, 2-Draht).
// Jeder Treiber bekommt eigene CLK-/DATA-Leitungen und 16 Byte Anzeigespeicher.
// ---------------------------------------------------------------------------
namespace Display {

constexpr int W = 42;
constexpr int H = 9;
constexpr int CHIPS = 3;
constexpr int RAM = 16;

// Fünf Zusatz-LEDs neben der Matrix (Statuspunkte)
enum Dot : uint8_t { DOT_LEFT_TOP, DOT_LEFT_MID, DOT_LEFT_BOTTOM, DOT_RIGHT_TOP, DOT_RIGHT_BOTTOM, DOT_COUNT };
struct Addr { uint8_t chip; uint8_t bit; };
// Lage der Statuspunkte im Treiberspeicher (Hardware-Eigenschaft der KORTLINJE-Platine)
constexpr Addr DOT_ADDR[DOT_COUNT] = {{1, 127}, {2, 126}, {1, 126}, {0, 127}, {0, 126}};

inline bool fb[H][W];          // logisches Bild, so wie man auf die Uhr schaut
inline bool dots[DOT_COUNT];
inline uint8_t ram[CHIPS][RAM];
inline uint8_t brightness = 2;  // 0..7
inline bool rotate180 = true;
inline bool enabled = true;
inline bool dirty = true;
inline uint32_t lastPush = 0;

// Testmodus: rohe Treiberwerte statt Bild (für die Testseite)
inline bool rawMode = false;
inline uint8_t rawRam[CHIPS][RAM];

// --- Bildpunkt -> Treiber/Bit -----------------------------------------------
// Jeder Treiber versorgt einen 14 Spalten breiten Block. Darin liegen die ersten
// 6 Spalten in einem Speicherbereich mit 6 Bit je Zeile, die übrigen 8 Spalten
// in einem zweiten mit 8 Bit je Zeile (beides absteigend adressiert).
inline Addr pixelAddr(int x, int y) {
  if (rotate180) { x = W - 1 - x; y = H - 1 - y; }
  const int block = x / 14;
  const int col = x % 14;
  Addr a;
  a.chip = (uint8_t)(CHIPS - 1 - block);
  a.bit = (uint8_t)(col < 6 ? 125 - 6 * y - col : 71 - 8 * y - (col - 6));
  return a;
}

// --- Bus ----------------------------------------------------------------------
inline void wait() { delayMicroseconds(10); }   // gleiches Timing wie kobebeefg (10 µs)

inline void busStart(uint8_t c) {
  digitalWrite(Pin::DATA[c], HIGH); digitalWrite(Pin::CLK[c], HIGH); wait();
  digitalWrite(Pin::DATA[c], LOW); wait();
  digitalWrite(Pin::CLK[c], LOW); wait();
}
inline void busStop(uint8_t c) {
  digitalWrite(Pin::CLK[c], LOW); digitalWrite(Pin::DATA[c], LOW); wait();
  digitalWrite(Pin::CLK[c], HIGH); wait();
  digitalWrite(Pin::DATA[c], HIGH); wait();
}
inline void busByte(uint8_t c, uint8_t v) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(Pin::CLK[c], LOW);
    digitalWrite(Pin::DATA[c], (v >> i) & 1);
    wait();
    digitalWrite(Pin::CLK[c], HIGH);
    wait();
  }
  digitalWrite(Pin::CLK[c], LOW);
}
inline void command(uint8_t c, uint8_t cmd) { busStart(c); busByte(c, cmd); busStop(c); }

inline void writeChip(uint8_t c, const uint8_t *data) {
  command(c, 0x40);                 // Daten schreiben, Adresse zählt selbst hoch
  busStart(c);
  busByte(c, 0xC0);                 // ab Adresse 0
  for (int i = 0; i < RAM; i++) busByte(c, data[i]);
  busStop(c);
  command(c, enabled ? (0x88 | (brightness & 7)) : 0x80);
}

// --- Bild -> Treiber ----------------------------------------------------------
inline void build() {
  memset(ram, 0, sizeof(ram));
  for (int y = 0; y < H; y++)
    for (int x = 0; x < W; x++)
      if (fb[y][x]) { Addr a = pixelAddr(x, y); ram[a.chip][a.bit >> 3] |= 1 << (a.bit & 7); }
  for (int i = 0; i < DOT_COUNT; i++)
    if (dots[i]) ram[DOT_ADDR[i].chip][DOT_ADDR[i].bit >> 3] |= 1 << (DOT_ADDR[i].bit & 7);
}

inline uint8_t sent[CHIPS][RAM];   // zuletzt übertragener Inhalt

inline void push() {
  const uint8_t (*src)[RAM] = rawMode ? rawRam : ram;
  for (uint8_t c = 0; c < CHIPS; c++) writeChip(c, src[c]);
  memcpy(sent, src, sizeof(sent));
  dirty = false;
  lastPush = millis();
}

// Im loop() aufrufen: überträgt nur bei Änderung, sonst zur Sicherheit jede Sekunde neu
inline void update() {
  if (!rawMode) build();
  const uint8_t (*src)[RAM] = rawMode ? rawRam : ram;
  if (dirty || memcmp(src, sent, sizeof(sent)) != 0 || millis() - lastPush > 1000) push();
}

inline void begin() {
  for (uint8_t c = 0; c < CHIPS; c++) {
    pinMode(Pin::CLK[c], OUTPUT); pinMode(Pin::DATA[c], OUTPUT);
    digitalWrite(Pin::CLK[c], HIGH); digitalWrite(Pin::DATA[c], HIGH);
  }
  delay(20);
  memset(fb, 0, sizeof(fb));
  memset(dots, 0, sizeof(dots));
  build();
  push();
}

// --- Zeichnen -----------------------------------------------------------------
inline void clear() { memset(fb, 0, sizeof(fb)); }
inline void set(int x, int y, bool on = true) {
  if (x < 0 || y < 0 || x >= W || y >= H) return;
  fb[y][x] = on;
}
inline void setDot(Dot d, bool on) { dots[d] = on; }
inline void setBrightness(uint8_t b) { brightness = b > 7 ? 7 : b; dirty = true; }

// Zeichen an Position x (oben y), gibt Breite zurück
inline int drawChar(int x, int y, char ch) {
  const Font::Glyph *g = Font::find(ch);
  for (int c = 0; c < g->width; c++)
    for (int r = 0; r < 7; r++)
      if (g->cols[c] & (1 << r)) set(x + c, y + r);
  return g->width;
}

// Umlaute auf die Sonderzeichen der Schrift abbilden (UTF-8 -> 1 Byte)
inline String normalize(const String &in) {
  String out; out.reserve(in.length());
  for (size_t i = 0; i < in.length(); i++) {
    uint8_t b = in[i];
    if (b == 0xC3 && i + 1 < in.length()) {
      uint8_t n = in[++i];
      if (n == 0x84 || n == 0xA4) out += '\x01';
      else if (n == 0x96 || n == 0xB6) out += '\x02';
      else if (n == 0x9C || n == 0xBC) out += '\x03';
      else if (n == 0x9F) out += "SS";
      else out += '?';
    } else if (b == 0xC2 && i + 1 < in.length() && (uint8_t)in[i + 1] == 0xB0) {
      out += '\x04'; i++;
    } else if (b == 0xE2 && i + 2 < in.length() && (uint8_t)in[i + 1] == 0x9C && (uint8_t)in[i + 2] == 0x88) {
      out += '\x06'; i += 2;              // ✈
    } else if (b == 0xE2 && i + 2 < in.length() && (uint8_t)in[i + 1] == 0x99 && (uint8_t)in[i + 2] == 0xA5) {
      out += '\x05'; i += 2;              // ♥
    } else if (b >= 0x80) {
      // anderes Mehrbyte-Zeichen überspringen
      while (i + 1 < in.length() && ((uint8_t)in[i + 1] & 0xC0) == 0x80) i++;
      out += '?';
    } else {
      out += (char)b;
    }
  }
  return out;
}

inline int textWidth(const String &t) {
  int w = 0;
  for (size_t i = 0; i < t.length(); i++) w += Font::find(t[i])->width + (i + 1 < t.length() ? 1 : 0);
  return w;
}

inline int drawText(int x, int y, const String &t) {
  for (size_t i = 0; i < t.length(); i++) x += drawChar(x, y, t[i]) + 1;
  return x;
}

inline void drawCentered(const String &t, int y = 1) { drawText((W - textWidth(t)) / 2, y, t); }

// Snapshot für die Live-Anzeige im Webinterface: alle Bildpunkte Zeile für Zeile,
// dann die Statuspunkte, je 4 Bit als ein Hex-Zeichen (96 statt 383 Zeichen)
inline String snapshotHex() {
  static const char *hx = "0123456789abcdef";
  String s; s.reserve(100);
  uint8_t nib = 0; int bits = 0;
  auto add = [&](bool on) { nib = (nib << 1) | (on ? 1 : 0); if (++bits == 4) { s += hx[nib]; nib = 0; bits = 0; } };
  for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) add(fb[y][x]);
  for (int i = 0; i < DOT_COUNT; i++) add(dots[i]);
  while (bits) add(false);
  return s;
}

}  // namespace Display
