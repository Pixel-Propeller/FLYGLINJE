#pragma once
#include <Arduino.h>
#include "esp_random.h"
#include "Display.h"
#include "Settings.h"
#include "Inputs.h"
#include "Badges.h"
#include "Album.h"
#include "QtData.h"

// ---------------------------------------------------------------------------
// Spiele auf der Matrix (42 × 9), gesteuert vom Handy über das Webinterface:
//  * Snake – Richtung per Steuerkreuz oder Wischen
//  * Pong  – Schläger per Finger auf dem Handy; gegen die Uhr oder zu zweit
//  * Quartett – zu zweit mit den Flugzeugtypen aus dem Sammelalbum (Karten nur auf den Handys)
// Ohne Eingabe endet ein Spiel nach 90 Sekunden.
// ---------------------------------------------------------------------------
namespace Game {

enum Kind : uint8_t { NONE, SNAKE, PONG, QUARTETT };
constexpr int W = Display::W, H = Display::H;

inline Kind kind = NONE;
inline uint32_t lastInput = 0;
inline int score[2] = {0, 0};
inline int highSnake = 0;
inline String endText;          // nach dem Spiel als Laufschrift
inline bool justEnded = false;  // App zeigt dann endText

inline uint32_t rnd(uint32_t n) { return esp_random() % n; }

// ---- Snake ----------------------------------------------------------------------
struct P { int8_t x, y; };
constexpr int MAXLEN = W * H;
inline P body[MAXLEN];
inline int len = 0, headIdx = 0;
inline char dir = 'R';
inline char queue[3]; inline int queued = 0;
inline P food;
inline uint32_t stepMs = 200, lastStep = 0;

inline bool onSnake(int x, int y) {
  for (int i = 0; i < len; i++) { const P &p = body[(headIdx - i + MAXLEN) % MAXLEN]; if (p.x == x && p.y == y) return true; }
  return false;
}
inline void placeFood() {
  if (len >= MAXLEN - 1) return;
  do { food.x = rnd(W); food.y = rnd(H); } while (onSnake(food.x, food.y));
}
inline void startSnake() {
  len = 3; headIdx = 2;
  for (int i = 0; i < 3; i++) body[i] = {(int8_t)(8 + i), (int8_t)4};
  dir = 'R'; queued = 0; stepMs = 200; lastStep = millis();
  score[0] = 0;
  placeFood();
}
inline bool opposite(char a, char b) {
  return (a == 'L' && b == 'R') || (a == 'R' && b == 'L') || (a == 'U' && b == 'D') || (a == 'D' && b == 'U');
}
inline void snakeInput(char d) {
  char last = queued ? queue[queued - 1] : dir;
  if (d == last || opposite(d, last)) return;
  if (queued >= 3) { queue[0] = queue[1]; queue[1] = queue[2]; queued = 2; }   // ältere verwerfen, neueste zählt
  queue[queued++] = d;
}

// ---- Pong -----------------------------------------------------------------------
constexpr float PAD = 3;                 // Schlägerhöhe
inline float bx, by, vx, vy;
inline float pad[2], target[2];
inline bool twoPlayer = false;
inline bool waitP2 = false;              // Pong zu zweit: wartet, bis Spieler 2 beitritt
inline uint32_t waitStart = 0;
inline int hlSide = -1;                  // dieser Schläger blinkt kurz (Seitenwahl am Handy)
inline uint32_t hlUntil = 0;
inline void highlight(int side) { if (side < 0 || side > 1) return; hlSide = side; hlUntil = millis() + 1500; lastInput = millis(); }
inline uint32_t lastFrame = 0, serveAt = 0;
constexpr int WIN = 5;

inline void serve(int towards) {
  bx = W / 2.0f; by = H / 2.0f;
  vx = towards ? 0.45f : -0.45f;
  vy = ((int)rnd(61) - 30) / 100.0f;
  serveAt = millis() + 900;
}
inline void startPong(bool two) {
  twoPlayer = two; waitP2 = false;
  score[0] = score[1] = 0;
  pad[0] = pad[1] = target[0] = target[1] = (H - PAD) / 2;
  serve(rnd(2));
  lastFrame = millis();
}

// ---- Quartett -------------------------------------------------------------------------
// Die Uhr verteilt die Karten und vergleicht die Werte (so kann niemand schummeln); jedes Handy
// sieht nur seine oberste Karte. Wer am Zug ist, wählt einen Wert, der höhere gewinnt beide
// Karten. Gleichstand: beide Karten kommen in den Topf, der nächste Stich nimmt ihn mit.
namespace Q {
constexpr int MAXC = 180;
inline uint8_t deck[2][MAXC];                // Ringpuffer mit Indizes in QtData::ROWS
inline int dh[2] = {0, 0}, dn[2] = {0, 0};
inline uint8_t pot[MAXC]; inline int potN = 0;
inline int turn = 0, phase = 0, per = 0, rounds = 0;   // phase 0 = Wert wählen, 1 = Auflösung
inline uint32_t revealAt = 0;
inline int lastCat = -1, lastWin = -1;       // lastWin: 0/1, -1 = Gleichstand
inline uint8_t lastCard[2]; inline bool over = false;
inline int cachedCount = -1; inline uint32_t cachedAt = 0;
inline void push(int p, uint8_t c) { if (dn[p] >= MAXC) return; deck[p][(dh[p] + dn[p]) % MAXC] = c; dn[p]++; }
inline uint8_t pop(int p) { uint8_t c = deck[p][dh[p]]; dh[p] = (dh[p] + 1) % MAXC; dn[p]--; return c; }
inline int top(int p) { return dn[p] ? deck[p][dh[p]] : -1; }

// Karten, die mitspielen dürfen: gesammelte Katalog-Typen mit Quartett-Werten
inline int eligible(uint8_t *out) {
  int n = 0;
  if (!Album::lock) return 0;
  xSemaphoreTake(Album::lock, portMAX_DELAY);
  for (int k = 0; k < Album::nTypes && n < MAXC; k++) {
    if (Album::catIndex(Album::types[k].code) < 0) continue;
    int q = QtData::find(Album::types[k].code);
    if (q >= 0) { if (out) out[n] = q; n++; }
  }
  xSemaphoreGive(Album::lock);
  return n;
}
inline int count() {                         // für die Startseite, kurz zwischengespeichert
  if (cachedCount < 0 || millis() - cachedAt > 10000) { cachedCount = eligible(nullptr); cachedAt = millis(); }
  return cachedCount;
}
inline const char *name(int q) { int ci = Album::catIndex(QtData::ROWS[q].code); return ci >= 0 ? Album::TYPES[ci].name : QtData::ROWS[q].code; }
inline int rar(int q) { int ci = Album::catIndex(QtData::ROWS[q].code); return ci >= 0 ? Album::TYPES[ci].rar : 0; }
}  // namespace Q

constexpr int Q_MIN = 5, Q_NEED = 10;
// false = zu wenige Karten gesammelt
inline bool startQuartett(int per) {
  static uint8_t all[Q::MAXC];
  int n = Q::eligible(all);
  if (n < Q_NEED) return false;
  for (int i = n - 1; i > 0; i--) { int j = rnd(i + 1); uint8_t t = all[i]; all[i] = all[j]; all[j] = t; }
  per = constrain(per, Q_MIN, n / 2);
  Q::dh[0] = Q::dh[1] = Q::dn[0] = Q::dn[1] = 0; Q::potN = 0;
  for (int i = 0; i < per * 2; i++) Q::push(i % 2, all[i]);
  Q::per = per; Q::turn = rnd(2); Q::phase = 0; Q::rounds = 0; Q::lastCat = -1; Q::lastWin = -1; Q::over = false;
  Q::cachedCount = n; Q::cachedAt = millis();
  return true;
}
// Spieler p wählt Wert cat (0..7); false = gerade nicht erlaubt
inline bool qPick(int p, int cat) {
  if (kind != QUARTETT || waitP2 || Q::phase != 0 || p != Q::turn || cat < 0 || cat >= QtData::CATS) return false;
  if (!Q::dn[0] || !Q::dn[1]) return false;
  int a = Q::top(0), b = Q::top(1);
  int32_t va = QtData::ROWS[a].v[cat], vb = QtData::ROWS[b].v[cat];
  if ((p == 0 ? va : vb) < 0) return false;  // den eigenen Wert gibt es nicht
  lastInput = millis();
  Q::pop(0); Q::pop(1);
  Q::lastCard[0] = a; Q::lastCard[1] = b; Q::lastCat = cat;
  int w = va > vb ? 0 : vb > va ? 1 : -1;
  Q::lastWin = w;
  if (w >= 0) {
    Q::push(w, w == 0 ? a : b); Q::push(w, w == 0 ? b : a);
    for (int i = 0; i < Q::potN; i++) Q::push(w, Q::pot[i]);
    Q::potN = 0; Q::turn = w;
    Buzzer::beep(w == 0 ? 2093 : 1568, 90, false);
  } else {
    if (Q::potN + 2 <= Q::MAXC) { Q::pot[Q::potN++] = a; Q::pot[Q::potN++] = b; }
    Buzzer::beep(1000, 60, false);
  }
  Q::rounds++;
  Q::over = !Q::dn[0] || !Q::dn[1] || Q::rounds >= 300;
  Q::phase = 1; Q::revealAt = millis();
  return true;
}
inline void qNext() { if (kind == QUARTETT && Q::phase == 1 && millis() - Q::revealAt > 800) Q::revealAt = millis() - 100000; }

// ---- Steuerung ----------------------------------------------------------------------
inline void finish(const String &text) { endText = text; justEnded = true; kind = NONE; waitP2 = false; }

inline void start(Kind k, bool two = false, bool wait = false) {
  kind = k; lastInput = millis(); justEnded = false;
  if (k == SNAKE) startSnake(); else if (k == PONG) startPong(two);
  if (k == PONG && two && wait) { waitP2 = true; waitStart = millis(); }
}
inline bool startQ(int per) {
  if (!startQuartett(per)) return false;
  kind = QUARTETT; twoPlayer = true; lastInput = millis(); justEnded = false;
  waitP2 = true; waitStart = millis();
  return true;
}
// Spieler 2 ist da: jetzt geht es los
inline bool joinP2() {
  if (kind == QUARTETT && waitP2) { waitP2 = false; lastInput = millis(); Buzzer::beep(1568, 60, false); return true; }
  if (kind != PONG || !waitP2) return false;
  waitP2 = false; lastInput = millis();
  Badges::earn(Badges::PONG2);
  score[0] = score[1] = 0; serve(rnd(2));
  highlight(1);
  return true;
}
inline void stop() { if (kind != NONE) kind = NONE; }
inline bool active() { return kind != NONE; }

inline void input(char d) { lastInput = millis(); if (kind == SNAKE) snakeInput(d); }
// Pong: Position 0..1000 (oben..unten) für Seite 0 (links) oder 1 (rechts)
inline void position(int side, int v) {
  lastInput = millis();
  if (kind != PONG || side < 0 || side > 1) return;
  target[side] = constrain(v, 0, 1000) / 1000.0f * (H - PAD);
}

// ---- Ablauf & Zeichnen (aus App::update, jede Schleife) ------------------------------------
inline void stepSnake(uint32_t now) {
  if (now - lastStep < stepMs) return;
  lastStep = now;
  if (queued) { dir = queue[0]; queue[0] = queue[1]; queue[1] = queue[2]; queued--; }
  P h = body[headIdx];
  if (dir == 'R') h.x++; else if (dir == 'L') h.x--; else if (dir == 'U') h.y--; else h.y++;
  bool grow = h.x == food.x && h.y == food.y;
  // Wand oder sich selbst getroffen? (das Schwanzende rückt gleich weiter)
  bool hitSelf = false;
  for (int i = 0; i < len - (grow ? 0 : 1); i++) { const P &p = body[(headIdx - i + MAXLEN) % MAXLEN]; if (p.x == h.x && p.y == h.y) hitSelf = true; }
  if (h.x < 0 || h.x >= W || h.y < 0 || h.y >= H || hitSelf) {
    Buzzer::beep(300, 400, false);
    bool record = score[0] > highSnake;
    if (record) { highSnake = score[0]; Settings::prefs.putUShort("g_snake", highSnake); }
    finish(String("ENDE  ") + score[0] + (score[0] == 1 ? " PUNKT" : " PUNKTE") + (record ? "  NEUER REKORD!" : ""));
    return;
  }
  headIdx = (headIdx + 1) % MAXLEN;
  body[headIdx] = h;
  if (grow) {
    len++; score[0]++;
    if (score[0] >= 20) Badges::earn(Badges::SNAKE);
    Buzzer::beep(2600, 30, false);
    if (stepMs > 90) stepMs -= 6;
    placeFood();
  }
}

inline void drawSnake(uint32_t now) {
  Display::clear();
  for (int i = 0; i < len; i++) { const P &p = body[(headIdx - i + MAXLEN) % MAXLEN]; Display::set(p.x, p.y); }
  if ((now / 250) % 2) Display::set(food.x, food.y);      // Futter blinkt
}

inline void cpuMove(int side) {
  // folgt dem Ball, aber nicht perfekt: reagiert nur, wenn der Ball kommt, und nur begrenzt schnell
  bool coming = side == 1 ? vx > 0 : vx < 0;
  float aim = coming ? by - PAD / 2 + ((int)rnd(3) - 1) * 0.4f : (H - PAD) / 2;
  float d = aim - pad[side];
  float maxStep = 0.22f;
  pad[side] += d > maxStep ? maxStep : d < -maxStep ? -maxStep : d;
}

inline void stepPong(uint32_t now) {
  if (now - lastFrame > 200) lastFrame = now - 30;         // nach einem Hänger nicht im Zeitraffer aufholen
  while (now - lastFrame >= 30) {                          // 33 Bilder/s
    lastFrame += 30;
    for (int s = 0; s < 2; s++) {
      if (s == 1 && !twoPlayer) { cpuMove(1); continue; }
      float d = target[s] - pad[s];
      pad[s] += d > 0.6f ? 0.6f : d < -0.6f ? -0.6f : d;
    }
    for (int s = 0; s < 2; s++) pad[s] = constrain(pad[s], 0.0f, (float)H - PAD);
    if ((int32_t)(now - serveAt) < 0) continue;            // kurze Pause vor dem Anstoß
    bx += vx; by += vy;
    if (by < 0) { by = -by; vy = -vy; }
    if (by > H - 1) { by = 2 * (H - 1) - by; vy = -vy; }
    for (int s = 0; s < 2; s++) {
      bool atPaddle = s == 0 ? (bx <= 1.0f && vx < 0) : (bx >= W - 2.0f && vx > 0);
      if (!atPaddle) continue;
      float rel = by - pad[s];                             // 0..PAD = getroffen
      if (rel >= -0.5f && rel <= PAD - 0.5f) {
        vx = -vx * 1.06f;
        vx = constrain(vx, -1.2f, 1.2f);
        vy += (rel - (PAD - 1) / 2) * 0.18f;
        vy = constrain(vy, -0.7f, 0.7f);
        bx = s == 0 ? 1.0f : W - 2.0f;
        Buzzer::beep(s == 0 ? 1800 : 1500, 15, false);
      }
    }
    if (bx < -0.5f || bx > W - 0.5f) {                      // Punkt
      int winner = bx < 0 ? 1 : 0;
      score[winner]++;
      Buzzer::beep(winner == 0 ? 2400 : 600, 120, false);
      if (score[winner] >= WIN) {
        char b[40];
        if (twoPlayer) snprintf(b, sizeof(b), "%s GEWINNT  %d:%d", winner == 0 ? "LINKS" : "RECHTS", score[0], score[1]);
        else snprintf(b, sizeof(b), "%s  %d:%d", winner == 0 ? "GEWONNEN!" : "VERLOREN", score[0], score[1]);
        if (!twoPlayer && winner == 0) Badges::earn(Badges::PONG);
        finish(b);
        return;
      }
      serve(winner == 0 ? 1 : 0);
    }
  }
}

inline void drawPong(uint32_t now) {
  Display::clear();
  for (int s = 0; s < 2; s++) {
    if (s == hlSide && (int32_t)(hlUntil - now) > 0 && (now % 300) < 150) continue;   // eigene Seite blinkt
    int top = (int)lroundf(pad[s]);
    for (int i = 0; i < (int)PAD; i++) Display::set(s == 0 ? 0 : W - 1, top + i);
  }
  // Mitte nur oben und unten markieren
  Display::set(W / 2, 0); Display::set(W / 2, H - 1);
  // Stand kurz in der Mitte zeigen, solange angestoßen wird
  if ((int32_t)(now - serveAt) < 0) {
    char b[8]; snprintf(b, sizeof(b), "%d:%d", score[0], score[1]);
    Display::clear();
    Display::drawCentered(b, 1);
    return;
  }
  Display::set((int)lroundf(bx), (int)lroundf(by));
}

// true = Spiel hat die Anzeige übernommen
inline bool update() {
  if (kind == NONE) return false;
  uint32_t now = millis();
  if (now - lastInput > (kind == QUARTETT ? 300000UL : 90000UL)) { finish("SPIEL BEENDET"); return false; }
  for (int i = 0; i < Display::DOT_COUNT; i++) Display::setDot((Display::Dot)i, false);
  if (waitP2) {
    if (now - waitStart > 180000) { finish("KEIN SPIELER 2"); return false; }
    // „WARTE AUF SPIELER 2“ läuft durch, links steht schon der eigene Schläger
    static const char *T = "WARTE AUF SPIELER 2";
    int w = Display::textWidth(T), span = w + Display::W;
    int x = Display::W - (int)((now - waitStart) / 45 % span);
    Display::clear();
    Display::drawText(x, 1, T);
    return true;
  }
  if (kind == SNAKE) { stepSnake(now); if (kind == SNAKE) drawSnake(now); }
  else if (kind == PONG) { stepPong(now); if (kind == PONG) drawPong(now); }
  else if (kind == QUARTETT) {
    if (Q::phase == 1 && now - Q::revealAt > 4500) {
      if (Q::over) {
        char b[48];
        if (Q::dn[0] == Q::dn[1]) snprintf(b, sizeof(b), "QUARTETT UNENTSCHIEDEN %d:%d", Q::dn[0], Q::dn[1]);
        else snprintf(b, sizeof(b), "SPIELER %d GEWINNT %d:%d", Q::dn[0] > Q::dn[1] ? 1 : 2, Q::dn[0], Q::dn[1]);
        Buzzer::beep(1568, 90); Buzzer::beep(0, 30); Buzzer::beep(2093, 90); Buzzer::beep(0, 30); Buzzer::beep(2637, 220);
        finish(b);
        return false;
      }
      Q::phase = 0;
    }
    Display::clear();
    char b[16];
    if (Q::phase == 1) snprintf(b, sizeof(b), Q::lastWin < 0 ? "PATT" : "STICH %d", Q::lastWin + 1);
    else snprintf(b, sizeof(b), "%d:%d", Q::dn[0], Q::dn[1]);
    Display::drawCentered(b, 1);
  }
  return kind != NONE;
}

inline void begin() { highSnake = Settings::prefs.getUShort("g_snake", 0); }

}  // namespace Game
