#pragma once
#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include "Config.h"
#include "Settings.h"
#include "Display.h"
#include "Inputs.h"
#include "Net.h"
#include "Alarm.h"
#include "Flight.h"
#include "Iss.h"
#include "Game.h"
#include "Profile.h"
#include "Weather.h"
#include "Extras.h"
#include "Stats.h"
#include "Badges.h"
#include "Tracker.h"
#include "Alerts.h"
#include "Album.h"
#include "esp_ota_ops.h"

// ---------------------------------------------------------------------------
// Was auf der Matrix zu sehen ist und was die Tasten tun:
// Uhr, Wecker (anzeigen, einstellen, klingeln), Laufschrift, Einblendungen,
// Einrichtungshinweise, Updates und Testbilder.
// ---------------------------------------------------------------------------
namespace App {

// ---- Laufschrift --------------------------------------------------------------
// Passt der Text auf die Anzeige, steht er still in der Mitte (2,5 s je Wiederholung);
// ist er breiter, läuft er durch.
struct Scroller {
  String text;
  int pos = 0, width = 0, repeats = 0;
  uint32_t last = 0, staticUntil = 0;
  bool still = false;
  uint16_t speed = 45;   // ms je Spalte
  bool active() const { return text.length() > 0; }
  void start(const String &t, int times = 1) {
    text = Display::normalize(t); width = Display::textWidth(text);
    pos = -Display::W; repeats = times; last = 0;
    still = width <= Display::W;
    staticUntil = millis() + 2500UL * (times > 0 ? times : 1);
  }
  void stop() { text = ""; }
  void draw() {
    uint32_t now = millis();
    if (still) {
      if ((int32_t)(now - staticUntil) >= 0) { stop(); Display::clear(); return; }
      Display::clear();
      Display::drawText((Display::W - width) / 2, 1, text);
      return;
    }
    if (now - last >= speed) {
      last = now;
      if (++pos > width) {
        if (repeats > 0 && --repeats == 0) { stop(); return; }
        pos = -Display::W;
      }
    }
    Display::clear();
    Display::drawText(-pos, 1, text);
  }
};

inline Scroller scroller;          // Nachrichten, Begrüßung, IP, Weckspruch
inline String overlay;             // kurze Einblendung (zu lange Texte laufen einmal durch)
inline uint32_t overlayUntil = 0, overlayStart = 0;
inline const char *const *overlayIcon = nullptr;   // Symbol links, Text dann rechtsbündig
inline uint8_t overlayIconH = 0;
inline bool overlayRunway = false;
inline int infoIndex = 0;
inline String updateText;          // "UPDATE" während eines Updates

// Testbetrieb (von der Testseite gesteuert)
enum TestMode { TEST_OFF, TEST_ALL, TEST_CHIP, TEST_SCAN, TEST_DOTS, TEST_TEXT, TEST_FLIGHT };
inline TestMode test = TEST_OFF;
inline int testChip = 0;
inline int scanPos = 0;
inline uint32_t testUntil = 0, scanLast = 0;
inline String testText;
inline bool testInit = false;
// stehen weiter unten (Flugansicht)
inline void flightTestFrame(bool init);
inline void flightTestEnd();
inline bool flightAlert();

// Zugang zurücksetzen per Taste beim Einschalten
inline bool resetCountdown = false;
inline uint32_t resetStart = 0;

// Einstellen per Tasten (Wecker oder Uhrzeit)
enum EditTarget { EDIT_NONE, EDIT_ALARM, EDIT_CLOCK };
enum EditField { F_HOUR, F_MIN, F_DAYS };
inline EditTarget edit = EDIT_NONE;
inline EditField field = F_HOUR;
inline int editIdx = 0;
inline int eh = 7, em = 0;
inline uint8_t edays = Alarm::DAYS_WORK;
inline uint32_t lastKeyAt = 0;
inline int viewAlarm = -1;          // Wecker gerade angezeigt (nach "Wecker"-Taste)
inline uint32_t viewUntil = 0;

inline const char *WEEKDAYS[] = {"SO", "MO", "DI", "MI", "DO", "FR", "SA"};
constexpr uint8_t DAY_PRESETS[] = {Alarm::DAYS_WORK, Alarm::DAYS_ALL, Alarm::DAYS_WEEKEND, Alarm::DAYS_ONCE};

inline void showOverlay(const String &t, uint16_t ms = 1500) {
  overlay = Display::normalize(t);
  overlayIcon = nullptr; overlayStart = millis();
  int w = Display::textWidth(overlay);
  uint32_t need = w > Display::W ? 700 + (uint32_t)(w - Display::W) * 55 + 900 : 0;   // einmal ganz durchlaufen
  overlayUntil = overlayStart + (need > ms ? need : ms);
}
// Einblendung mit Symbol links (z. B. Flugzeug + „AN“)
inline void showIconOverlay(const char *const *icon, uint8_t h, bool runway, const String &t, uint16_t ms = 1500) {
  showOverlay(t, ms);
  overlayIcon = icon; overlayIconH = h; overlayRunway = runway;
}
inline void drawIcon(const char *const *icon, uint8_t h, int x, int y) {
  for (int r = 0; r < h; r++)
    for (int c = 0; icon[r][c]; c++)
      if (icon[r][c] == '#') Display::set(x + c, y + r);
}
inline void drawOverlay(uint32_t now) {
  Display::clear();
  int w = Display::textWidth(overlay);
  if (overlayIcon) {
    drawIcon(overlayIcon, overlayIconH, 0, 0);
    if (overlayRunway) for (int c = 0; c < 12; c++) Display::set(c, 7);
    Display::drawText(Display::W - w, overlayRunway ? 0 : 1, overlay);
    return;
  }
  if (w <= Display::W) { Display::drawCentered(overlay); return; }
  int el = (int)(now - overlayStart) - 700;
  int shift = el > 0 ? el / 55 : 0;
  if (shift > w - Display::W) shift = w - Display::W;
  Display::drawText(-shift, 1, overlay);
}

// ---- Symbole (Vorlage: Flughafen-Piktogramme Abflug/Ankunft, darunter die Bahn) ----
inline const char *const DEP_ICON[7] = {
  "..........##",
  "..###...###.",
  "...#######..",
  "....####....",
  "#####.......",
  ".##.........",
  "............",
};
inline const char *const ARR_ICON[7] = {
  ".#...#......",
  "###..##.....",
  ".######.....",
  "...######...",
  "......####..",
  "........###.",
  "............",
};
inline const char *const THERMO_ICON[8] = {
  "..#..", ".#.#.", ".#.#.", ".#.#.", ".###.", "#####", "#####", ".###.",
};
inline const char *const UMBRELLA_ICON[8] = {
  "..###..", ".#####.", "#######", "...#...", "...#...", "...#...", ".#.#...", "..#....",
};
inline const char *const HOURGLASS_ICON[7] = {
  "#####", "#...#", ".#.#.", "..#..", ".#.#.", "#...#", "#####",
};
inline const char *const SUN_ICON[7] = {
  "#..#..#", ".#...#.", "..###..", "###.###", "..###..", ".#...#.", "#..#..#",
};
inline const char *const ISS_ICON[7] = {
  "##...##", "##...##", "##.#.##", "#######", "##.#.##", "##...##", "##...##",
};
inline const char *const BELL_ICON[7] = {
  "...#...", "..###..", ".#####.", ".#####.", ".#####.", "#######", "...#...",
};
inline const char *const SPEAKER_ICON[7] = {
  "...#....", "..##..#.", "####...#", "####.#.#", "####...#", "..##..#.", "...#....",
};

// ---- Modus-Menü auf „Schlummern“: jeder Druck zeigt den nächsten Modus,
// hinten „+“ schaltet ihn an, „−“ aus. Nach 6 s ohne Taste zurück zur Uhr.
enum ModeItem : uint8_t { MI_FLIGHT, MI_WEATHER, MI_ISS, MI_ALARMS, MI_SOUND, MI_TIMER, MI_COUNT };
inline const char *MODE_NAMES[MI_COUNT] = {"FLUG", "WETTER", "ISS", "WECKER", "TON", "TIMER"};
inline int modeSel = -1;
inline uint32_t restShownAt = 0;
inline uint32_t modeUntil = 0, modeNameUntil = 0;

inline bool modeGet(int m) {
  const Profile::P &pp = Profile::p[Profile::active];
  switch (m) {
    case MI_FLIGHT: return pp.flight;
    case MI_WEATHER: return Weather::enabled;
    case MI_ISS: return pp.iss;
    case MI_ALARMS: return Alarm::master;
    case MI_SOUND: return pp.sound;
    case MI_TIMER: return Extras::timerRunning();
    default: return false;
  }
}
inline void modeSet(int m, bool on) {
  if (modeGet(m) == on) return;
  Profile::P &pp = Profile::p[Profile::active];
  switch (m) {
    case MI_FLIGHT: pp.flight = on; Profile::changed(Profile::active); Profile::update(); break;
    case MI_WEATHER: Weather::enabled = on; if (on) Weather::refresh = true; Weather::save(); break;
    case MI_ISS: pp.iss = on; Profile::changed(Profile::active); Profile::update(); break;
    case MI_ALARMS: Alarm::master = on; Alarm::save(); break;
    case MI_SOUND: pp.sound = on; Profile::changed(Profile::active); Profile::update(); break;
  }
}
inline void modeShow(int m, bool withName) {
  modeSel = m; modeUntil = millis() + 6000;
  modeNameUntil = withName ? millis() + 900 : 0;
}
inline bool modeActive() { return modeSel >= 0 && (int32_t)(modeUntil - millis()) > 0; }
inline bool drawModeMenu(uint32_t now) {
  if (modeSel < 0) return false;
  if ((int32_t)(modeUntil - now) <= 0) { modeSel = -1; return false; }
  Display::clear();
  const char *name = MODE_NAMES[modeSel];
  if ((int32_t)(modeNameUntil - now) > 0) { Display::drawCentered(name); return true; }
  bool on = modeGet(modeSel);
  bool runway = false;
  switch (modeSel) {
    case MI_FLIGHT: drawIcon(on ? DEP_ICON : ARR_ICON, 7, 0, 0); runway = true; break;
    case MI_WEATHER: drawIcon(SUN_ICON, 7, 0, 1); break;
    case MI_ISS: drawIcon(ISS_ICON, 7, 0, 1); break;
    case MI_ALARMS: drawIcon(BELL_ICON, 7, 0, 1); break;
    case MI_SOUND: drawIcon(SPEAKER_ICON, 7, 0, 1); break;
    case MI_TIMER: drawIcon(HOURGLASS_ICON, 7, 0, 1); break;
  }
  if (runway) for (int c = 0; c < 12; c++) Display::set(c, 7);
  String tt = modeSel == MI_TIMER && on ? Extras::timerText(Extras::timerLeftMs()) : String("");
  const char *st = tt.length() ? tt.c_str() : on ? "AN" : "AUS";
  Display::drawText(Display::W - Display::textWidth(st), runway ? 0 : 1, st);
  // unten: ein Punkt je Modus, der aktuelle blinkt (wie bei den Flügen)
  if (!runway) for (int i = 0; i < MI_COUNT; i++) if (i != modeSel || (now % 600) < 380) Display::set(12 + i * 2, 8);
  return true;
}

inline void message(const String &t, int times = 2) { scroller.start(t, times); }
inline String queued;              // läuft danach (z. B. Begrüßung nach der Update-Meldung)

// ---- Startanimation: ein Flugzeug fliegt quer über die Anzeige ---------------------
// Draufsicht, Nase nach rechts (9 Zeilen hoch, 14 breit), dahinter ein Kondensstreifen.
inline const char *PLANE[9] = {
  "......#.......",
  "......##......",
  "......###.....",
  "#.....####....",
  "#############.",
  "#.....####....",
  "......###.....",
  "......##......",
  "......#.......",
};
constexpr int PLANE_W = 14;
constexpr uint32_t INTRO_MS = 2600;
inline uint32_t introStart = 0;
inline bool introOn = false;
inline String introFirst, introSecond;   // danach: Update-Meldung und/oder Begrüßung

inline void drawIntro(uint32_t now) {
  uint32_t t = now - introStart;
  int x = -PLANE_W + (int)((uint64_t)t * (Display::W + PLANE_W * 2) / INTRO_MS);
  Display::clear();
  for (int r = 0; r < 9; r++)
    for (int c = 0; c < PLANE_W; c++)
      if (PLANE[r][c] == '#') Display::set(x + c, r);
  // Kondensstreifen: gestrichelt hinter dem Heck, wandert mit
  for (int k = x - 2; k >= 0 && k > x - 30; k--) if (((k - x) & 3) != 0 && ((x - k) % 7) < 4) Display::set(k, 4);
}
inline void endIntro() {
  introOn = false; Display::clear();
  if (introFirst.length()) message(introFirst, 1);
  if (introSecond.length()) queued = introSecond;
  introFirst = ""; introSecond = "";
}

inline void startTest(TestMode m, int chip = 0, const String &text = "") {
  if (test == TEST_FLIGHT && m != TEST_FLIGHT) flightTestEnd();
  test = m; testChip = chip; testText = Display::normalize(text); testInit = true;
  scanPos = 0; scanLast = 0;
  testUntil = millis() + 120000;     // nach 2 Minuten automatisch zurück zur Uhr
  Display::rawMode = (m == TEST_ALL || m == TEST_CHIP || m == TEST_SCAN);
  Display::dirty = true;
}
inline void stopTest() {
  if (test == TEST_FLIGHT) flightTestEnd();
  test = TEST_OFF; Display::rawMode = false;
  for (int i = 0; i < Display::DOT_COUNT; i++) Display::setDot((Display::Dot)i, false);
  Display::clear();
}

// ---- Zeichnen -------------------------------------------------------------------
// Uhrzeit HH:MM; einzelne Teile können ausgeblendet werden (zum Blinken beim Einstellen)
inline void drawTime(int h, int m, bool showH = true, bool showM = true, bool colon = true) {
  char hh[3], mm[3];
  snprintf(hh, sizeof(hh), "%02d", h);
  snprintf(mm, sizeof(mm), "%02d", m);
  const int total = 5 + 1 + 5 + 2 + 1 + 2 + 5 + 1 + 5;
  int x = (Display::W - total) / 2;
  if (showH) { Display::drawChar(x, 1, hh[0]); Display::drawChar(x + 6, 1, hh[1]); }
  x += 13;
  if (colon) Display::drawChar(x, 1, ':');
  x += 3;
  if (showM) { Display::drawChar(x, 1, mm[0]); Display::drawChar(x + 6, 1, mm[1]); }
}

inline bool nowTm(struct tm &t) {
  if (!Net::timeValid()) return false;
  time_t now = time(nullptr);
  localtime_r(&now, &t);
  return true;
}

inline void drawClock() {
  Display::clear();
  struct tm t;
  if (!nowTm(t)) { Display::drawCentered("--:--"); return; }
  bool colon = !Settings::d.colonBlink || (millis() % 1000) < 500;
  drawTime(t.tm_hour, t.tm_min, true, true, colon);
}

inline String dateText() {
  struct tm t;
  if (!nowTm(t)) return "KEINE ZEIT";
  char b[16];
  snprintf(b, sizeof(b), "%s %02d.%02d", WEEKDAYS[t.tm_wday], t.tm_mday, t.tm_mon + 1);
  return b;
}

// Statuspunkte (die Namen DOT_LEFT/RIGHT stammen aus der Treiber-Zählung; weil die
// Anzeige um 180° gedreht ist, liegt DOT_LEFT_MID von vorne gesehen rechts in der Mitte):
// Wecker 1, Wecker 2, Schlummern = rechts Mitte (leuchtet durchgehend), Flugmodus aktiv
// (Lage der Punkte auf der Testseite unter "Status-LEDs" prüfen)
// Welche Punkte was zeigen, ist unter Test › Statuspunkte einstellbar (Settings::d.dotRole)
enum DotRole { ROLE_W1, ROLE_W2, ROLE_SNOOZE, ROLE_FLIGHT };
inline void setRole(DotRole r, bool on) {
  uint8_t i = Settings::d.dotRole[r];
  if (i < Display::DOT_COUNT && on) Display::setDot((Display::Dot)i, true);
}
inline void drawDots() {
  for (int i = 0; i < Display::DOT_COUNT; i++) Display::setDot((Display::Dot)i, false);
  setRole(ROLE_W1, Alarm::master && Alarm::alarms[0].on);
  setRole(ROLE_W2, Alarm::master && Alarm::alarms[1].on);
  setRole(ROLE_SNOOZE, Alarm::state == Alarm::SNOOZING);
  // Flugmodus-Punkt; blinkt schnell, solange ein Flug mit Notfall angezeigt wird
  setRole(ROLE_FLIGHT, Flight::activeNow() && (!flightAlert() || (millis() % 500) < 250));
}

inline void drawTest() {
  uint32_t now = millis();
  if (now > testUntil) { stopTest(); return; }
  switch (test) {
    case TEST_ALL:
      memset(Display::rawRam, 0xFF, sizeof(Display::rawRam));
      break;
    case TEST_CHIP:
      memset(Display::rawRam, 0, sizeof(Display::rawRam));
      memset(Display::rawRam[testChip], 0xFF, Display::RAM);
      break;
    case TEST_SCAN:
      if (now - scanLast > 120) {
        scanLast = now;
        memset(Display::rawRam, 0, sizeof(Display::rawRam));
        for (int c = 0; c < Display::CHIPS; c++) Display::rawRam[c][scanPos / 8] = 1 << (scanPos % 8);
        scanPos = (scanPos + 1) % (Display::RAM * 8);
      }
      break;
    case TEST_DOTS:
      Display::clear();
      // testChip = Nummer des Punkts (0–4) oder -1 für alle
      Display::drawCentered(testChip < 0 ? String("PUNKTE") : "PUNKT " + String(testChip + 1));
      for (int i = 0; i < Display::DOT_COUNT; i++) Display::setDot((Display::Dot)i, testChip < 0 || testChip == i);
      break;
    case TEST_TEXT:
      Display::clear();
      Display::drawCentered(testText);
      break;
    case TEST_FLIGHT:                       // Beispielflug (besondere Flugzeuge, Notfälle)
      flightTestFrame(testInit);
      testInit = false;
      break;
    default: break;
  }
}

inline String setupHint() {
  return "WLAN " + Net::apName + "  PASSWORT " + Settings::d.apPass + "  DANN " + Net::ip() + " ÖFFNEN  ";
}

inline String alarmLabel(int i) {
  const Alarm::Entry &e = Alarm::alarms[i];
  if (!e.on) return "W" + String(i + 1) + " AUS";
  char b[20]; snprintf(b, sizeof(b), "W%d %02d:%02d", i + 1, e.hour % 24, e.minute % 60);
  return b;
}

// ---- Einstellen per Tasten ------------------------------------------------------------
inline void beginEdit(EditTarget target, int idx) {
  edit = target; editIdx = idx; field = F_HOUR; lastKeyAt = millis();
  if (target == EDIT_ALARM) {
    const Alarm::Entry &e = Alarm::alarms[idx];
    eh = e.hour; em = e.minute; edays = e.days;
  } else {
    struct tm t;
    if (nowTm(t)) { eh = t.tm_hour; em = t.tm_min; } else { eh = 12; em = 0; }
  }
  overlayUntil = 0; viewAlarm = -1;
}

inline void finishEdit() {
  if (edit == EDIT_ALARM) {
    Alarm::Entry &e = Alarm::alarms[editIdx];
    e.hour = eh; e.minute = em; e.days = edays; e.on = true;
    Alarm::save();
    showOverlay(alarmLabel(editIdx), 1500);
  } else if (edit == EDIT_CLOCK) {
    // Uhrzeit von Hand setzen (für den Betrieb ohne WLAN). Datum bleibt, falls bekannt.
    struct tm t;
    if (!nowTm(t)) { memset(&t, 0, sizeof(t)); t.tm_year = 2026 - 1900; t.tm_mon = 0; t.tm_mday = 1; }
    t.tm_hour = eh; t.tm_min = em; t.tm_sec = 0; t.tm_isdst = -1;
    time_t v = mktime(&t);
    struct timeval tv = {v, 0};
    settimeofday(&tv, nullptr);
    showOverlay("ZEIT OK");
  }
  edit = EDIT_NONE;
}

inline void editKey(Keys::Key k, bool repeat) {
  using namespace Keys;
  lastKeyAt = millis();
  int dir = (k == PLUS) ? 1 : (k == MINUS) ? -1 : 0;
  if (dir) {
    int stepMin = repeat ? 5 : 1;
    if (field == F_HOUR) eh = (eh + dir + 24) % 24;
    else if (field == F_MIN) em = (em + dir * stepMin + 60 * 5) % 60;
    else {
      int p = 0;
      for (int i = 0; i < 4; i++) if (DAY_PRESETS[i] == edays) p = i;
      p = (p + dir + 4) % 4;
      edays = DAY_PRESETS[p];
    }
    return;
  }
  if (k == SET || k == ALARM) {
    if (field == F_HOUR) field = F_MIN;
    else if (field == F_MIN && edit == EDIT_ALARM) field = F_DAYS;
    else finishEdit();
    return;
  }
  // andere Taste: abbrechen
  edit = EDIT_NONE;
  showOverlay("ABBRUCH", 900);
}

inline void drawEdit() {
  Display::clear();
  bool blinkOff = (millis() % 600) > 380;
  if (field == F_DAYS) {
    if (!blinkOff) Display::drawCentered(Display::normalize(Alarm::daysLabel(edays)));
  } else {
    drawTime(eh, em, !(field == F_HOUR && blinkOff), !(field == F_MIN && blinkOff), true);
  }
  // kleiner Hinweis unten: W1/W2/Uhr
  int bar = edit == EDIT_CLOCK ? 0 : editIdx + 1;
  for (int i = 0; i < 3; i++) Display::set(i * 3, 8, i == bar);
}

// ---- Nachtmodus -------------------------------------------------------------------
// Im eingestellten Zeitraum dunkelste Stufe oder Anzeige ganz aus (spart den
// größten Teil des Stroms: die LEDs). Eine Taste weckt die Anzeige für 15 s.
inline uint32_t wakeUntil = 0;

// Ist im aktiven Profil (Tag oder Nacht) „Anzeige aus“ gewählt?
inline bool profileDark() { return Profile::p[Profile::active].dark; }
inline bool nightTime() { return profileDark(); }            // erste Taste weckt nur die Anzeige
inline bool displayDark() {
  return profileDark() && (int32_t)(wakeUntil - millis()) <= 0 && Alarm::state != Alarm::RINGING;
}

// ---- Tasten ---------------------------------------------------------------------
inline void handleKeys() {
  using namespace Keys;
  Key k = pressed != NONE ? pressed : longPressed;
  bool isLong = longPressed != NONE;
  // geheime Nachricht: „Helligkeit“ 5 s halten
  if (veryLong == BRIGHTNESS && Extras::secret.length()) {
    wakeUntil = millis() + 15000; overlayUntil = 0; modeSel = -1;
    message("\x05 " + Extras::secret + " \x05", 1);
    Badges::earn(Badges::SECRET);
    return;
  }
  if (k == NONE && repeated == NONE) return;

  // Nachts: erste Taste weckt nur die Anzeige
  if (nightTime()) {
    bool wasDark = displayDark();
    wakeUntil = millis() + 15000;
    if (wasDark && Alarm::state != Alarm::RINGING) return;
  }

  // Während des Klingelns: Schlummern schlummert, jede andere Taste stoppt
  if (Alarm::state == Alarm::RINGING) {
    if (k == NONE) k = repeated;
    scroller.stop();
    if (k == SNOOZE && Alarm::snooze()) showOverlay("ZZ " + String(Alarm::snoozeMin) + "MIN", 1500);
    else if (k == SNOOZE) showOverlay("AUS", 900);             // Schlummern für diesen Wecker aus
    else { Alarm::stop(); showOverlay("AUS", 900); }
    return;
  }
  // Timer klingelt: jede Taste stellt ihn ab
  if (Extras::timerRinging && k != NONE) { Extras::timerStop(); noTone(Pin::BUZZER); showOverlay("TIMER AUS", 900); return; }
  // Schlummern läuft: Schlummern-Taste zeigt Restzeit, Wecker-Taste beendet
  if (Alarm::state == Alarm::SNOOZING && (k == ALARM || k == BELL)) { Alarm::stop(); showOverlay("WECKER AUS", 1200); return; }

  if (k != NONE) Buzzer::click();
  // Modus-Menü offen: + = an, − = aus; jede andere Taste außer Schlummern schließt es
  if (modeActive() && edit == EDIT_NONE) {
    Key mk = k != NONE ? k : repeated;
    if (mk == PLUS || mk == MINUS) {
      if (modeSel == MI_TIMER) { Extras::timerAdd(mk == PLUS ? 1 : -1); modeShow(modeSel, false); }   // je 1 Minute, Halten = schnell
      else if (k != NONE) { modeSet(modeSel, mk == PLUS); modeShow(modeSel, false); }
      return;
    }
    if (k != SNOOZE || isLong) modeSel = -1;
  }
  if (k == NONE && edit == EDIT_NONE) {           // Helligkeit mit +/- halten
    if (viewAlarm >= 0) return;
    k = repeated;
  }

  if (edit != EDIT_NONE) {
    if (k != NONE) editKey(k, false);
    else editKey(repeated, true);   // +/- gedrückt halten = schnell
    return;
  }

  switch (k) {
    case BRIGHTNESS: {
      if (isLong) break;                     // Halten: nichts (5 s = geheime Nachricht)
      uint8_t b = (Settings::d.brightness + 1) % 8;
      Profile::p[Profile::active].bright = b; Profile::changed(Profile::active); Profile::update();
      Display::setBrightness(b);
      showOverlay("HELL " + String(b + 1));
      break;
    }
    case PLUS: case MINUS: {
      if (viewAlarm >= 0) {                 // Wecker wird angezeigt: +/- schaltet ihn an/aus
        Alarm::alarms[viewAlarm].on = !Alarm::alarms[viewAlarm].on; Alarm::save();
        showOverlay(alarmLabel(viewAlarm), 2000); viewUntil = millis() + 2500;
        break;
      }
      int b = constrain((int)Settings::d.brightness + (k == PLUS ? 1 : -1), 0, 7);
      Profile::p[Profile::active].bright = b; Profile::changed(Profile::active); Profile::update();
      Display::setBrightness(b);
      showOverlay("HELL " + String(b + 1));
      break;
    }
    case MODE:
      // lang (1 s): IP-Adresse bzw. Einrichtungs-WLAN – kurz: Datum → Begrüßung → Uhr
      if (isLong) {
        infoIndex = 0; overlayUntil = 0;
        if (Net::state == Net::SETUP_AP) message(setupHint(), 1);   // Heimnetz weg: Einrichtungs-WLAN zeigen
        else message(Net::ip().length() ? "IP " + Net::ip() : "OFFLINE", 1);
        break;
      }
      // Datum → Temperatur → Regen → Unwetter → verfolgter Flug → Flüge heute → Begrüßung → Uhr
      // (jeweils nur, wenn es etwas zu zeigen gibt)
      {
        float wt; int wr;
        bool wx = Weather::enabled && Weather::snapshot(wt, wr);
        bool st = Flight::cfg.enabled || Stats::today() > 0;
        String uw = Alerts::ticker(), tr = Tracker::ticker();
        scroller.stop();
        auto skip = [&](int i) { return (!wx && (i == 2 || i == 3)) || (i == 3 && wr < 0) || (i == 4 && uw.isEmpty()) ||
                                        (i == 5 && tr.isEmpty()) || (!st && i == 6); };
        do { infoIndex = (infoIndex + 1) % 8; } while (skip(infoIndex));
        char b[12];
        switch (infoIndex) {
          case 1: showOverlay(dateText(), 2500); break;
          case 2: snprintf(b, sizeof(b), "%d\x04" "C", (int)lroundf(wt)); showIconOverlay(THERMO_ICON, 8, false, b, 2500); break;
          case 3: snprintf(b, sizeof(b), "%d%%", wr); showIconOverlay(UMBRELLA_ICON, 8, false, b, 2500); break;
          case 4: overlayUntil = 0; message(uw, 1); break;
          case 5: overlayUntil = 0; message(tr, 1); break;
          case 6: showOverlay("HEUTE " + String(Stats::today()) + " FLÜGE", 2500); break;
          case 7: overlayUntil = 0; message(Settings::d.greeting.length() ? Settings::d.greeting : String("HALLO!"), 1); break;
          default: overlayUntil = 0; break;
        }
      }
      break;
    case BELL:
      if (isLong) {
        Profile::p[Profile::active].sound = !Profile::p[Profile::active].sound;
        Profile::changed(Profile::active); Profile::update();
        showOverlay(Settings::d.sound ? "TON AN" : "TON AUS");
      } else {
        Alarm::master = !Alarm::master; Alarm::save();
        showOverlay(Alarm::master ? "WECKER AN" : "WECKER AUS", 1500);
      }
      break;
    case SNOOZE:
      if (isLong) {                          // 1 s halten: Flugmodus an/aus (im gerade aktiven Profil)
        Profile::P &pp = Profile::p[Profile::active];
        pp.flight = !pp.flight;
        Profile::changed(Profile::active); Profile::update();
        // Abheben = an, Landen = aus
        showIconOverlay(pp.flight ? DEP_ICON : ARR_ICON, 7, true, pp.flight ? "AN" : "AUS", 1800);
        break;
      }
      // Schlummern läuft: erst die Restzeit, ein weiterer Druck (innerhalb 3 s) öffnet das Menü
      if (Alarm::state == Alarm::SNOOZING && !modeActive() && millis() - restShownAt > 3000) {
        int left = (int)((Alarm::snoozeUntil - millis()) / 60000) + 1;
        showOverlay("ZZ " + String(left) + "MIN", 1500);
        restShownAt = millis();
        break;
      }
      overlayUntil = 0; scroller.stop();
      modeShow(modeActive() ? (modeSel + 1) % MI_COUNT : 0, true);
      break;
    case ALARM:
      if (isLong) { beginEdit(EDIT_ALARM, viewAlarm >= 0 ? viewAlarm : 0); break; }
      viewAlarm = (viewAlarm + 1) % 2;
      viewUntil = millis() + 3000;
      showOverlay(alarmLabel(viewAlarm), 3000);
      break;
    case SET:
      if (viewAlarm >= 0) beginEdit(EDIT_ALARM, viewAlarm);   // Wecker angezeigt -> einstellen
      else if (isLong) beginEdit(EDIT_CLOCK, 0);              // lang -> Uhrzeit von Hand stellen
      else showOverlay(dateText(), 2000);
      break;
    default: break;
  }
}


// ---- Flugmodus auf der Matrix ------------------------------------------------------
// Ablauf: Flug 1 mit allen gewählten Seiten (je 3 s), dann Flug 2 … danach 6 s Uhr.
// Unterste Zeile: links ein Balken je Flug (aktueller Flug breit), rechts ein Punkt je Seite.
namespace FlightView {
inline Flight::Plane list[Flight::MAX];
inline int n = 0, idx = 0;
constexpr uint8_t PAGE_SPECIAL = 255;      // „NOTFALL“, „A380“ … steht vor den anderen Seiten
inline uint8_t pages[Flight::PAGE_COUNT + 1];
inline String texts[Flight::PAGE_COUNT + 1];
inline int pageCount = 0, page = 0;
inline uint32_t pageStart = 0, clockUntil = 0;
inline bool running = false;
inline int resumeIdx = -1;          // nach der Uhr-Pause mit diesem Flug weitermachen
inline bool demo = false;           // Test: Beispielflug statt echter Daten
inline uint8_t alertPrio = 0;       // höchste Stufe der gerade gezeigten Flüge

// Signalton bei einem Notfall: je Flug nur einmal (die letzten 6 merken)
inline char alerted[6][7];
inline int alertNext = 0;
inline void alertOnce(const Flight::Plane &p, bool force) {
  if (!force) {
    for (auto &h : alerted) if (h[0] && strcmp(h, p.hex) == 0) return;
    strncpy(alerted[alertNext], p.hex, 6); alerted[alertNext][6] = 0; alertNext = (alertNext + 1) % 6;
  }
  for (int i = 0; i < 3; i++) { Buzzer::beep(2400, 120, force); Buzzer::beep(0, 80, force); Buzzer::beep(1800, 120, force); Buzzer::beep(0, 200, force); }
}

inline void loadPages() {
  pageCount = 0;
  Flight::Plane &p = list[idx];
  // Route frisch aus der Liste holen: sie kommt oft erst nach dem ersten Abruf
  if (!demo) {
    char f[5], t[5]; bool ch = false;
    if (Flight::routeFor(p.hex, f, t, ch)) { strcpy(p.from, f); strcpy(p.to, t); p.routeChecked = ch; }
  }
  Flight::Special sp = Flight::special(p);
  if (sp.prio) { texts[pageCount] = Display::normalize(sp.text); pages[pageCount++] = PAGE_SPECIAL; }
  uint8_t mask = demo ? 0xFF : Flight::cfg.pages;          // im Test alle Seiten
  for (Flight::Page pg : Flight::PAGE_ORDER) {
    if (!(mask & (1 << pg))) continue;
    String t = Display::normalize(Flight::pageText(p, pg));
    if (t.isEmpty()) continue;
    texts[pageCount] = t; pages[pageCount++] = pg;
  }
  if (pageCount == 0) { texts[0] = p.call; pages[0] = Flight::P_CALL; pageCount = 1; }
  page = 0; pageStart = millis();
  if (sp.prio == 3) alertOnce(p, demo);
}

inline uint32_t pageDuration(const String &t) {
  int w = Display::textWidth(t);
  return w <= Display::W ? 3000 : 800 + (uint32_t)(w - Display::W) * 60 + 800;
}

inline void updateAlert() {
  alertPrio = 0;
  for (int i = 0; i < n; i++) { uint8_t pr = Flight::special(list[i]).prio; if (pr > alertPrio) alertPrio = pr; }
}

// Aktuelle Seite zeichnen; false = alle Seiten dieses Flugs gezeigt
inline bool drawPage(uint32_t now) {
  if (now - pageStart > pageDuration(texts[page])) {
    if (++page >= pageCount) return false;
    pageStart = now;
  }
  const String &txt = texts[page];
  Display::clear();
  // Notfall: der Hinweis blinkt
  bool hide = pages[page] == PAGE_SPECIAL && Flight::special(list[idx]).prio == 3 && (now % 700) >= 480;
  int w = Display::textWidth(txt);
  int x = (Display::W - w) / 2;
  if (w > Display::W) {                       // zu lang: langsam durchschieben
    int elapsed = (int)(now - pageStart) - 800;
    int shift = elapsed > 0 ? elapsed / 60 : 0;
    if (shift > w - Display::W) shift = w - Display::W;
    x = -shift;
  }
  if (pages[page] == Flight::P_ROUTE || pages[page] == Flight::P_TO) {
    // links das Symbol (Abflug bzw. Ankunft) über der Bahn, rechtsbündig der Flughafen
    drawIcon(pages[page] == Flight::P_TO ? ARR_ICON : DEP_ICON, 7, 0, 0);
    for (int c = 0; c < 12; c++) Display::set(c, 7);
    Display::drawText(Display::W - w, 0, txt);
  } else if (!hide) Display::drawText(x, 0, txt);
  // Unterste Zeile: links ein Punkt je Flugzeug, rechts ein Punkt je Info-Seite
  // dieses Flugs. Der Punkt für das aktuelle Flugzeug bzw. die aktuelle Seite blinkt.
  if (Flight::cfg.bar || demo) {
    bool on = (now % 600) < 380;
    for (int i = 0; i < n; i++)
      if (i != idx || on) Display::set(i, 8);
    for (int i = 0; i < pageCount; i++)
      if (i != page || on) Display::set(Display::W - pageCount + i, 8);
  }
  return true;
}
}  // namespace FlightView

inline bool flightAlert() { return FlightView::alertPrio == 3 && (FlightView::running || FlightView::demo); }

inline bool drawFlights() {
  using namespace FlightView;
  uint32_t now = millis();
  if (!Flight::activeNow()) { running = false; resumeIdx = -1; alertPrio = 0; return false; }
  if ((int32_t)(clockUntil - now) > 0) return false;           // Uhr-Pause zwischen den Durchläufen

  if (!running) {
    n = Flight::snapshot(list, Flight::cfg.maxPlanes);
    updateAlert();
    if (n == 0) { clockUntil = now + 3000; return false; }
    idx = 0; running = true; resumeIdx = -1; loadPages();
  } else if (resumeIdx >= 0) {
    idx = resumeIdx; resumeIdx = -1; loadPages();
  }
  if (!drawPage(now)) {
    // nach jedem Flug die Uhr zeigen (einstellbar), damit die Uhrzeit nie lange fehlt
    running = ++idx < n;
    clockUntil = now + (uint32_t)Flight::cfg.clockGapS * 1000UL;
    if (running) { pageStart = 0; resumeIdx = idx; }
    return false;
  }
  return true;
}

// ---- Wetter: in einstellbarem Abstand 3 s Temperatur, 3 s Regenwahrscheinlichkeit ----------
namespace WeatherView {
inline bool on = false;
inline uint32_t start = 0, lastShow = 0;
inline float t = 0; inline int rain = -1;
}
inline bool drawWeather() {
  using namespace WeatherView;
  uint32_t now = millis();
  if (!Weather::enabled) { on = false; return false; }
  if (!on) {
    if (lastShow && now - lastShow < (uint32_t)Weather::everyMin * 60000UL) return false;
    if (!Weather::snapshot(t, rain)) return false;
    on = true; start = now;
  }
  uint32_t el = now - start;
  int page = el < 3000 ? 0 : el < 6000 ? 1 : 2;
  if (page == 1 && rain < 0) page = 2;
  if (page == 2) { on = false; lastShow = now; return false; }
  Display::clear();
  char b[12];
  if (page == 0) {
    snprintf(b, sizeof(b), "%d\x04" "C", (int)lroundf(t));
    drawIcon(THERMO_ICON, 8, 0, 0);
  } else {
    snprintf(b, sizeof(b), "%d%%", rain);
    drawIcon(UMBRELLA_ICON, 8, 0, 0);
  }
  Display::drawText(Display::W - Display::textWidth(b), 1, b);
  return true;
}

// Test › Besondere Flugzeuge: ein Beispielflug läuft in Schleife durch alle Seiten
inline void flightTestFrame(bool init) {
  using namespace FlightView;
  uint32_t now = millis();
  if (init) {
    demo = true; running = false; resumeIdx = -1;
    list[0] = Flight::demoPlane(testChip); n = 1; idx = 0;
    updateAlert(); loadPages();
  }
  for (int i = 0; i < Display::DOT_COUNT; i++) Display::setDot((Display::Dot)i, false);
  setRole(ROLE_FLIGHT, alertPrio < 3 || (now % 500) < 250);
  if (!drawPage(now)) { page = 0; pageStart = now; drawPage(now); }
}
inline void flightTestEnd() {
  FlightView::demo = false; FlightView::running = false; FlightView::n = 0; FlightView::alertPrio = 0;
  FlightView::clockUntil = 0;
}

// ---- ISS-Überflug auf der Matrix ---------------------------------------------------------
// ab 6 h vorher: jede Minute 3 s „[ISS] 21:43“ · ab 10 min: abwechselnd Countdown und Uhr,
// unten läuft ein Balken voll · während des Überflugs: „SW 35°“ (wohin schauen),
// „SW>NO“ (von wo nach wo) und „MAX 62°“, unten der Fortschritt.
namespace IssView {
inline int64_t beepFor = 0;
inline uint8_t beepStage = 0;
}

inline void bottomBar(float frac) {
  if (frac < 0) frac = 0; if (frac > 1) frac = 1;
  int w = (int)(frac * Display::W + 0.5f);
  for (int x = 0; x < w; x++) Display::set(x, 8);
}

inline bool drawIss() {
  if (!Iss::cfg.enabled || !Net::timeValid()) return false;
  int64_t now = (int64_t)time(nullptr);
  Sat::Pass p;
  if (!Iss::next(p, now)) return false;
  uint32_t ms = millis();
  int64_t toStart = p.start - now;

  // Ton: 10 Minuten vorher zweimal, beim Aufgang einmal
  if (IssView::beepFor != p.start) { IssView::beepFor = p.start; IssView::beepStage = toStart <= 600 ? (toStart <= 0 ? 2 : 1) : 0; }
  if (Iss::cfg.beep && Alarm::state == Alarm::IDLE) {
    if (IssView::beepStage == 0 && toStart <= 600) { IssView::beepStage = 1; Buzzer::beep(2400, 120, true); }
    if (IssView::beepStage == 1 && toStart <= 0) { IssView::beepStage = 2; Buzzer::beep(2800, 250, true); }
  }

  if (now >= p.start && now <= p.end) {                      // Überflug läuft
    Badges::earn(Badges::ISS);
    int pg = (ms / 3000) % 3;
    char b[20];
    if (pg == 0 && now - Iss::liveAt <= 2 && Iss::liveEl > 0)
      snprintf(b, sizeof(b), "%s %d\x04", Sat::compass8(Iss::liveAz), (int)lroundf(Iss::liveEl));
    else if (pg == 2)
      snprintf(b, sizeof(b), "MAX %d\x04", (int)lroundf(p.maxEl));
    else
      snprintf(b, sizeof(b), "%s>%s", Sat::compass8(p.azStart), Sat::compass8(p.azEnd));
    Display::clear();
    Display::drawCentered(b, 0);
    bottomBar((float)(now - p.start) / (float)(p.end - p.start));
    return true;
  }
  if (toStart > 0 && toStart <= 600) {                       // Countdown
    if ((ms / 5000) % 2 == 1) return false;                  // jede zweite 5-s-Phase: Uhr
    char b[20];
    if (toStart >= 60) snprintf(b, sizeof(b), "\x07 %dMIN", (int)((toStart + 59) / 60));
    else snprintf(b, sizeof(b), "\x07 %dS", (int)toStart);
    Display::clear();
    Display::drawCentered(b, 0);
    bottomBar((600.0f - (float)toStart) / 600.0f);
    return true;
  }
  if (Iss::cfg.announce && toStart > 0 && toStart <= 6 * 3600 && (ms % 60000) < 3000) {
    time_t st = (time_t)p.start; struct tm t; localtime_r(&st, &t);
    char b[20]; snprintf(b, sizeof(b), "\x07 %02d:%02d", t.tm_hour, t.tm_min);
    Display::clear();
    Display::drawCentered(b, 1);
    return true;
  }
  return false;
}

// ---- Hauptschleife -----------------------------------------------------------------
inline void begin() {
  Display::rotate180 = Settings::d.rotate180;
  Display::setBrightness(Settings::d.brightness);
  // Nach einem Update: erst „UPDATE AUF VERSION …“, dann die Begrüßung, dann die Uhr
  String last = Settings::prefs.getString("lastver", "");
  if (last != FW_VERSION) Settings::prefs.putString("lastver", FW_VERSION);
  // frisch per WLAN installiert? (Rollback-Schutz: die neue Version wartet noch auf ihre Bestätigung)
  esp_ota_img_states_t st;
  bool pending = esp_ota_get_state_partition(esp_ota_get_running_partition(), &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  if (pending || (last.length() && last != FW_VERSION)) {
    introFirst = "UPDATE AUF VERSION " FW_VERSION;
    introSecond = Settings::d.greeting;
  } else {
    introFirst = Settings::d.greeting;
  }
  if (Settings::d.intro) { introOn = true; introStart = millis(); }
  else endIntro();
}

// ---- Extras: Lichtwecker, Wetter nach dem Wecker, Timer, „Über dir“, besondere Tage ----------
inline int lightLevel = -1;
inline uint32_t wakeInfoAt = 0;
inline uint8_t prevAlarmState = Alarm::IDLE;
inline bool prevPreview = false;

inline void songIfBirthday(bool afterAlarm);   // weiter unten
inline String wakeText() {
  String t;
  float wt; int wr;
  if (Extras::wakeWeather && Weather::enabled && Weather::snapshot(wt, wr)) {
    t = "HEUTE " + String((int)lroundf(wt)) + "\x04" "C";
    if (wr >= 0) t += "  REGEN " + String(wr) + "%";
    if (wr >= 50) t += "  SCHIRM MITNEHMEN!";
  }
  bool today = false;
  String d = Extras::dayMessage(&today);
  if (today) t += (t.length() ? "    " : "") + d;
  return t;
}

inline void extrasTick(uint32_t now) {
  // Lichtwecker
  lightLevel = -1;
  if (Extras::lightAlarm && Alarm::master && Alarm::state == Alarm::IDLE) {
    int best = -1;
    for (int i = 0; i < Alarm::COUNT; i++) { int m = Alarm::minutesUntil(Alarm::alarms[i]); if (m >= 0 && (best < 0 || m < best)) best = m; }
    if (best >= 1 && best <= 10) { lightLevel = (10 - best) * 7 / 9; wakeUntil = now + 3000; }
  }
  // Wecker gerade ausgeschaltet (nicht geschlummert, kein Probealarm): Wetter & Tag zeigen
  static uint8_t snoozes = 0;
  if (prevAlarmState == Alarm::RINGING && Alarm::state == Alarm::SNOOZING && !prevPreview && ++snoozes >= 3) Badges::earn(Badges::SLEEPY);
  if (prevAlarmState == Alarm::RINGING && Alarm::state == Alarm::IDLE && !prevPreview) {
    wakeInfoAt = now + 1200;
    struct tm t;
    if (snoozes == 0 && nowTm(t) && t.tm_hour < 6) Badges::earn(Badges::EARLY);
    snoozes = 0;
  }
  prevAlarmState = Alarm::state; prevPreview = Alarm::preview;
  if (wakeInfoAt && (int32_t)(now - wakeInfoAt) >= 0) {
    wakeInfoAt = 0;
    String t = wakeText();
    if (t.length()) { wakeUntil = now + 30000; message(t, 1); }
    songIfBirthday(true);
  }
  // Timer abgelaufen?
  if (Extras::timerRunning() && Extras::timerLeftMs() <= 0) {
    Extras::timerEnd = 0; Extras::timerRinging = true; Extras::timerRingStart = now;
    Badges::earn(Badges::TIMER);
  }
  if (Extras::timerRinging) {
    wakeUntil = now + 5000;
    if (now - Extras::timerRingStart > 60000) Extras::timerStop();
  }
}

// Timer klingelt: blinkendes „ZEIT!“ und Piepen (auch wenn Töne aus sind)
inline bool drawTimerRing(uint32_t now) {
  if (!Extras::timerRinging) return false;
  static uint32_t lastBeep = 0;
  if (now - lastBeep >= 1000) {
    lastBeep = now;
    Buzzer::beep(2600, 90, true); Buzzer::beep(0, 70, true); Buzzer::beep(2600, 90, true); Buzzer::beep(0, 70, true); Buzzer::beep(2600, 90, true);
  }
  Display::clear();
  if ((now % 700) < 450) { drawIcon(HOURGLASS_ICON, 7, 0, 1); Display::drawText(Display::W - Display::textWidth("ZEIT!"), 1, "ZEIT!"); }
  return true;
}
// Timer läuft: Sanduhr + Restzeit statt der Uhrzeit
inline bool drawTimer(uint32_t now) {
  if (!Extras::timerRunning()) return false;
  Display::clear();
  drawIcon(HOURGLASS_ICON, 7, 0, 1);
  String t = Extras::timerText(Extras::timerLeftMs());
  Display::drawText(Display::W - Display::textWidth(t), 1, t);
  return true;
}

inline void checkOverheadMsg() {
  static uint32_t seen = 0;
  uint32_t v = Flight::overVersion;
  if (v == seen) return;
  seen = v;
  if (!Extras::overhead || !Flight::activeNow()) return;
  xSemaphoreTake(Flight::lock, portMAX_DELAY); String c = Flight::overCall; xSemaphoreGive(Flight::lock);
  showOverlay("\x06 \x03" "BER DIR!  " + c, 3000);
  Buzzer::beep(2000, 80); Buzzer::beep(0, 60); Buzzer::beep(2600, 120);
}

// ---- Geburtstagsständchen („Happy Birthday“, gemeinfrei) mit Konfetti auf der Matrix ----
inline uint32_t partyUntil = 0;
inline void playSong(bool force) {
  struct N { uint16_t f, ms; };
  // zwei Oktaven höher als notiert: dort ist der Piezo am lautesten
  static const N S[] = {
    {1568, 300}, {1568, 100}, {1760, 400}, {1568, 400}, {2093, 400}, {1976, 800},
    {1568, 300}, {1568, 100}, {1760, 400}, {1568, 400}, {2349, 400}, {2093, 800},
    {1568, 300}, {1568, 100}, {3136, 400}, {2637, 400}, {2093, 400}, {1976, 400}, {1760, 800},
    {2794, 300}, {2794, 100}, {2637, 400}, {2093, 400}, {2349, 400}, {2093, 1000}};
  uint32_t total = 0;
  for (const N &n : S) { Buzzer::beep(n.f, n.ms * 85 / 100, force); Buzzer::beep(0, n.ms * 15 / 100, force); total += n.ms; }
  partyUntil = millis() + total + 500;
  wakeUntil = partyUntil + 5000;
  if (force || Settings::d.sound) Badges::earn(Badges::SONG);
}
// am besonderen Tag einmal: nach dem Wecker oder ab 8 Uhr
inline void songIfBirthday(bool afterAlarm) {
  if (!Extras::song || !Extras::dayCount) return;
  struct tm t;
  if (!nowTm(t)) return;
  int key = t.tm_year * 400 + t.tm_yday;
  if (Extras::songDay == key || (!afterAlarm && t.tm_hour < 8)) return;
  bool today = false;
  String m = Extras::dayMessage(&today);
  if (!today) return;
  Extras::songDay = key; Settings::prefs.putInt("x_songd", key);
  if (!scroller.active()) message(m, 2);
  playSong(false);
}
// Konfetti: zufällige Punkte flackern über die Laufschrift
inline void sparkle(uint32_t now) {
  if ((int32_t)(partyUntil - now) <= 0) return;
  for (int i = 0; i < 7; i++) Display::set(esp_random() % Display::W, esp_random() % Display::H);
}

// ---- Abzeichen, verfolgter Flug, Unwetter: Meldungen auf der Uhr ------------------------
inline bool newsTick(uint32_t now) {
  if (scroller.active() || (int32_t)(overlayUntil - now) > 0) return false;
  int id = Badges::takeNew();
  if (id >= 0) {
    wakeUntil = now + 15000;
    message(String("ABZEICHEN: ") + Badges::DEFS[id].name + "!", 1);
    Buzzer::beep(1568, 80); Buzzer::beep(0, 30); Buzzer::beep(2093, 80); Buzzer::beep(0, 30); Buzzer::beep(2637, 200);
    return true;
  }
  // neue Karten im Sammelalbum werden nur im Webinterface gezeigt, nicht auf der Uhr
  static uint32_t landedSeen = 0;
  if (Tracker::landedVersion != landedSeen) {
    landedSeen = Tracker::landedVersion;
    wakeUntil = now + 30000;
    xSemaphoreTake(Tracker::lock, portMAX_DELAY); String c = Tracker::call, to = Tracker::to; xSemaphoreGive(Tracker::lock);
    message("\x06 " + c + " IST GELANDET" + (to.length() ? " IN " + to : String("")) + "!", 2);
    for (int i = 0; i < 2; i++) { Buzzer::beep(2093, 120); Buzzer::beep(0, 60); Buzzer::beep(2637, 120); Buzzer::beep(0, 60); Buzzer::beep(3136, 240); Buzzer::beep(0, 300); }
    return true;
  }
  static uint32_t alertSeen = 0;
  if (Alerts::version != alertSeen) {
    alertSeen = Alerts::version;
    String t = Alerts::ticker();
    if (t.length()) {
      wakeUntil = now + 20000;
      message(t, 2);
      int n = Alerts::topLevel() >= 3 ? 3 : 1;
      for (int i = 0; i < n; i++) { Buzzer::beep(1400, 200); Buzzer::beep(0, 120); }
      return true;
    }
  }
  return false;
}
// Regelmäßige Erinnerungen: verfolgter Flug alle 3 min, Unwetter alle 30 min
inline bool remindersDue(uint32_t now) {
  static uint32_t lastTrack = 0, lastAlert = 0;
  if (Tracker::state == Tracker::AIR && now - lastTrack > 180000UL) {
    lastTrack = now;
    String t = Tracker::ticker();
    if (t.length()) { message(t, 1); return true; }
  }
  if (now - lastAlert > 30UL * 60000UL) {
    lastAlert = now;
    String t = Alerts::ticker();
    if (t.length()) { message(t, 1); return true; }
  }
  return false;
}

// Besondere Tage: am Tag selbst alle 5 Minuten, Countdown alle 30 Minuten
inline bool dayMessageDue(uint32_t now) {
  static uint32_t last = 0, every = 0;
  if (!Extras::dayCount || now < 20000) return false;
  if (last && now - last < every) return false;
  last = now;
  bool today = false;
  String m = Extras::dayMessage(&today);
  every = (today ? 5UL : 30UL) * 60000UL;
  if (m.isEmpty()) { every = 10UL * 60000UL; return false; }
  message(m, 1);
  return true;
}

inline void update() {
  uint32_t now = millis();

  // Zugang zurücksetzen: Schlummern beim Einschalten 10 s halten
  if (resetCountdown) {
    if (Keys::mvTop > Keys::TOLERANCE) { resetCountdown = false; Display::clear(); }
    else {
      int left = 10 - (int)((now - resetStart) / 1000);
      Display::clear();
      if (left > 0) Display::drawCentered("RESET " + String(left));
      else {
        Settings::resetLogin();
        resetCountdown = false;
        Buzzer::beep(1500, 400, true);
        message("ZUGANG ADMIN/ADMIN", 1);
      }
      return;
    }
  }

  if (Profile::update()) wakeUntil = 0;
  extrasTick(now);
  if (introOn && (Keys::pressed != Keys::NONE || Keys::longPressed != Keys::NONE)) { endIntro(); return; }   // Taste überspringt
  handleKeys();
  if (viewAlarm >= 0 && (int32_t)(now - viewUntil) > 0) viewAlarm = -1;
  if (edit != EDIT_NONE && now - lastKeyAt > 20000) { edit = EDIT_NONE; showOverlay("ABBRUCH", 900); }

  // Nachts Anzeige aus (nie während Update, Test oder Klingeln)
  bool dark = displayDark() && !Net::otaRunning && updateText.isEmpty() && test == TEST_OFF;
  if (Display::enabled == dark) { Display::enabled = !dark; Display::dirty = true; }
  // nachts aus oder während eines Spiels: keine Flugabfragen (das WLAN bleibt frei für die Steuerung)
  Flight::paused = dark || Game::active();

  if (Net::otaRunning || updateText.length()) {
    Display::clear();
    Display::drawCentered("UPDATE");
    int w = Net::otaPercent * Display::W / 100;
    for (int x = 0; x < w; x++) Display::set(x, 8);
    return;
  }

  if (test != TEST_OFF) { drawTest(); return; }

  if (introOn && Alarm::state != Alarm::RINGING) {
    if (millis() - introStart < INTRO_MS) { for (int i = 0; i < Display::DOT_COUNT; i++) Display::setDot((Display::Dot)i, false); drawIntro(millis()); return; }
    endIntro();
  } else if (introOn) endIntro();

  drawDots();

  // Wecker klingelt: erst der Weckspruch, dann blinkende Uhrzeit
  if (Alarm::state == Alarm::RINGING) {
    if (Alarm::justStarted) {
      Alarm::justStarted = false;
      Game::stop();                          // Wecker geht vor
      Display::setBrightness(7);
      if (Alarm::text.length()) message(Alarm::text, 1); else scroller.stop();
    }
    if (scroller.active()) { scroller.draw(); return; }
    Display::clear();
    if ((now % 1000) < 650) {
      struct tm t;
      if (nowTm(t)) drawTime(t.tm_hour, t.tm_min); else Display::drawCentered("WECKER");
    }
    return;
  }
  // Helligkeit / Nachtmodus (Lichtwecker: in den letzten 10 Minuten heller)
  uint8_t wantBright = Settings::d.brightness;          // kommt aus dem aktiven Profil (Tag/Nacht)
  if (lightLevel > (int)wantBright) wantBright = lightLevel;
  if (Display::brightness != wantBright) Display::setBrightness(wantBright);
  if (dark) { Display::clear(); return; }

  // Spiel (Snake/Pong) übernimmt die Anzeige; danach einmal das Ergebnis als Laufschrift
  if (Game::justEnded) { Game::justEnded = false; Net::applyPower(); Flight::refreshNow = true; message(Game::endText, 1); }
  if (Game::active()) {
    wakeUntil = now + 15000;                 // auch im Nachtmodus sichtbar
    if (Game::update()) return;
  }

  if (edit != EDIT_NONE) { drawEdit(); return; }
  if (drawModeMenu(now)) return;
  if (drawTimerRing(now)) return;
  checkOverheadMsg();

  if (Net::justConnected && !scroller.active()) {
    Net::justConnected = false;
    message("ONLINE  " + WiFi.localIP().toString() + "  " + Settings::d.hostname + ".LOCAL", 1);
  }

  if (!scroller.active() && queued.length()) { message(queued, 1); queued = ""; }
  newsTick(now);
  if (scroller.active()) { scroller.draw(); sparkle(now); return; }

  if ((int32_t)(overlayUntil - now) > 0) {
    drawOverlay(now);
    return;
  }

  if (Net::state == Net::SETUP_AP && Settings::d.wifiSsid.isEmpty()) {
    scroller.start(setupHint(), 1);    // läuft immer wieder, bis das WLAN eingerichtet ist
    return;
  }

  if (drawTimer(now)) return;
  if (drawIss()) return;
  if (WeatherView::on && drawWeather()) return;   // angefangene Wetteranzeige zu Ende zeigen
  if (drawFlights()) return;
  if (drawWeather()) return;
  if (dayMessageDue(now)) return;
  if (remindersDue(now)) return;
  songIfBirthday(false);
  drawClock();
}

}  // namespace App
