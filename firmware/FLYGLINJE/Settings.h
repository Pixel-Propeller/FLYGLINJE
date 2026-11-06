#pragma once
#include <Arduino.h>
#include <Preferences.h>
#include <MD5Builder.h>
#include "mbedtls/sha256.h"
#include "mbedtls/gcm.h"
#include "mbedtls/pkcs5.h"
#include "mbedtls/md.h"
#include "esp_random.h"
#include "Config.h"

// Alle dauerhaften Einstellungen. Gespeichert im NVS (überlebt Updates und Stromausfall).
//
// Geheimnisse (WLAN-Passwort, Passwort des Einrichtungs-WLANs, OTA-Schlüssel)
// liegen verschlüsselt (AES-256-GCM) im Speicher. Der Schlüssel entsteht aus einem
// zufälligen Gerätegeheimnis und der festen Chip-Kennung (eFuse-MAC). Wer nur den
// Speicherinhalt ausliest oder kopiert, sieht keine Passwörter im Klartext.
// Ehrlich gesagt: Wer das Gerät in der Hand hat UND die ganze Firmware analysiert,
// kann den Schlüssel nachbauen. Dagegen hilft nur die Flash-Verschlüsselung des
// ESP32 (eFuse, nicht umkehrbar) – für dieses Geschenk bewusst nicht aktiviert.
//
// Das Zugangspasswort wird gar nicht gespeichert, nur ein PBKDF2-Hash
// (HMAC-SHA256, 4096 Runden, zufälliges Salz).
namespace Settings {

struct Data {
  String wifiSsid;
  String wifiPass;
  String hostname;
  String user;
  String passSalt;
  String passHash;      // PBKDF2-SHA256(passwort, salt), hex
  uint8_t hashVer;      // 1 = altes SHA-256 (0.3.x), 2 = PBKDF2
  String otaHash;       // MD5(passwort) für Updates aus der Arduino IDE (verschlüsselt gespeichert)
  bool   ideOta;        // Updates aus der Arduino IDE erlauben (Standard: aus)
  String apPass;        // Passwort des Einrichtungs-WLANs (zufällig je Gerät)
  bool   defaultLogin;  // true = noch admin/admin
  String tz;
  uint8_t brightness;   // 0..7
  bool   rotate180;
  bool   colonBlink;
  bool   sound;
  String greeting;      // persönlicher Text beim Einschalten
  bool   intro;         // Startanimation (Flugzeug) beim Einschalten
  bool   keysSeen;      // Tasten wurden schon einmal erkannt
  String updateUrl;     // Adresse der Update-Datei auf der eigenen Webseite
  bool   autoUpdate;    // nachts automatisch installieren
  bool   nightOn;       // Nachtmodus
  uint16_t nightFrom, nightTo;   // Minuten nach Mitternacht
  uint8_t nightMode;    // 0 = dunkelste Stufe, 1 = Anzeige aus
  bool   powerSave;     // WLAN-Stromsparen + 160 MHz
  uint8_t dotRole[4];   // welcher Statuspunkt (0–4, 255 = keiner) zeigt: Wecker 1, Wecker 2, Schlummern, Flugmodus
};

inline Data d;
inline uint32_t bootCount = 0;   // Starts seit Werksreset (zur Fehlersuche)
inline Preferences prefs;

inline String toHex(const uint8_t *buf, size_t n) {
  static const char *hx = "0123456789abcdef";
  String s; s.reserve(n * 2);
  for (size_t i = 0; i < n; i++) { s += hx[buf[i] >> 4]; s += hx[buf[i] & 15]; }
  return s;
}

inline String randomHex(size_t bytes) {
  uint8_t b[32];
  if (bytes > sizeof(b)) bytes = sizeof(b);
  esp_fill_random(b, bytes);
  return toHex(b, bytes);
}

inline String sha256Hex(const String &text) {
  uint8_t out[32];
  mbedtls_sha256((const unsigned char *)text.c_str(), text.length(), out, 0);
  return toHex(out, 32);
}

inline String md5Hex(const String &text) {
  MD5Builder m; m.begin(); m.add(text); m.calculate();
  return m.toString();
}

// ---- Verschlüsselung der Geheimnisse ----------------------------------------------
constexpr int PBKDF2_ROUNDS = 4096;
inline uint8_t key[32];
inline bool keyReady = false;

inline void deriveKey() {
  uint8_t dev[32];
  if (prefs.getBytesLength("dsec") == sizeof(dev)) prefs.getBytes("dsec", dev, sizeof(dev));
  else { esp_fill_random(dev, sizeof(dev)); prefs.putBytes("dsec", dev, sizeof(dev)); }
  uint64_t mac = ESP.getEfuseMac();
  uint8_t mat[16 + 32 + 8];
  memcpy(mat, "FLYGLINJE-key-v1", 16);
  memcpy(mat + 16, dev, 32);
  memcpy(mat + 48, &mac, 8);
  mbedtls_sha256(mat, sizeof(mat), key, 0);
  memset(dev, 0, sizeof(dev)); memset(mat, 0, sizeof(mat));
  keyReady = true;
}

// Ablage: 12 Byte Zufallszahl (IV) | 16 Byte Prüfwert (Tag) | verschlüsselter Text
inline void putSecret(const char *name, const String &plain) {
  if (plain.isEmpty()) { prefs.remove(name); return; }
  size_t n = plain.length();
  uint8_t *blob = (uint8_t *)malloc(28 + n);
  if (!blob) return;
  esp_fill_random(blob, 12);
  mbedtls_gcm_context g; mbedtls_gcm_init(&g);
  mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  mbedtls_gcm_crypt_and_tag(&g, MBEDTLS_GCM_ENCRYPT, n, blob, 12, (const uint8_t *)name, strlen(name),
                            (const uint8_t *)plain.c_str(), blob + 28, 16, blob + 12);
  mbedtls_gcm_free(&g);
  prefs.putBytes(name, blob, 28 + n);
  free(blob);
}

inline String getSecret(const char *name) {
  size_t len = prefs.getBytesLength(name);
  if (len <= 28 || len > 300) return "";
  uint8_t blob[300]; char out[273];
  prefs.getBytes(name, blob, len);
  size_t n = len - 28;
  mbedtls_gcm_context g; mbedtls_gcm_init(&g);
  mbedtls_gcm_setkey(&g, MBEDTLS_CIPHER_ID_AES, key, 256);
  int r = mbedtls_gcm_auth_decrypt(&g, n, blob, 12, (const uint8_t *)name, strlen(name), blob + 12, 16, blob + 28, (uint8_t *)out);
  mbedtls_gcm_free(&g);
  if (r != 0) return "";            // manipuliert oder von einem anderen Gerät kopiert
  out[n] = 0;
  String s(out);
  memset(out, 0, sizeof(out));
  return s;
}

// Altes Klartext-Feld (bis 0.3.x) verschlüsselt übernehmen und löschen
inline String migrateSecret(const char *oldName, const char *newName) {
  if (prefs.isKey(oldName)) {
    String v = prefs.getString(oldName, "");
    putSecret(newName, v);
    prefs.remove(oldName);
    return v;
  }
  return getSecret(newName);
}

inline String pbkdf2Hex(const String &pass, const String &salt) {
  uint8_t out[32];
  mbedtls_pkcs5_pbkdf2_hmac_ext(MBEDTLS_MD_SHA256, (const uint8_t *)pass.c_str(), pass.length(),
                                (const uint8_t *)salt.c_str(), salt.length(), PBKDF2_ROUNDS, sizeof(out), out);
  return toHex(out, sizeof(out));
}

// Passwort für das Einrichtungs-WLAN: 8 Zeichen ohne Verwechsler (kein 0/O, 1/I)
inline String newApPass() {
  static const char *A = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
  uint8_t r[8]; esp_fill_random(r, sizeof(r));
  String s;
  for (int i = 0; i < 8; i++) s += A[r[i] & 31];
  return s;
}

// Vergleich ohne Zeitunterschied (gegen Rate-Angriffe)
inline bool safeEquals(const String &a, const String &b) {
  if (a.length() != b.length()) return false;
  uint8_t diff = 0;
  for (size_t i = 0; i < a.length(); i++) diff |= a[i] ^ b[i];
  return diff == 0;
}

inline void setPassword(const String &user, const String &pass, bool isDefault) {
  d.user = user;
  d.passSalt = randomHex(16);
  d.passHash = pbkdf2Hex(pass, d.passSalt);
  d.hashVer = 2;
  d.otaHash = md5Hex(pass);
  d.defaultLogin = isDefault;
  prefs.putString("user", d.user);
  prefs.putString("salt", d.passSalt);
  prefs.putString("hash", d.passHash);
  prefs.putUChar("hver", 2);
  putSecret("otax", d.otaHash);
  prefs.remove("ota");
  prefs.putBool("deflogin", d.defaultLogin);
}

inline bool checkLogin(const String &user, const String &pass) {
  if (user != d.user) { pbkdf2Hex(pass, d.passSalt); return false; }   // gleiche Rechenzeit
  if (d.hashVer < 2) {
    // Hash aus 0.3.x: prüfen und bei Erfolg gleich auf PBKDF2 umstellen
    bool ok = safeEquals(sha256Hex(d.passSalt + pass), d.passHash);
    if (ok) setPassword(user, pass, d.defaultLogin);
    return ok;
  }
  return safeEquals(pbkdf2Hex(pass, d.passSalt), d.passHash);
}

inline void resetLogin() { setPassword(DEFAULT_USER, DEFAULT_PASS, true); }

inline void begin() {
  prefs.begin("flyglinje", false);
  bootCount = prefs.getUInt("boots", 0) + 1;
  prefs.putUInt("boots", bootCount);
  deriveKey();
  d.wifiSsid   = prefs.getString("ssid", "");
  d.wifiPass   = migrateSecret("wpass", "wpx");
  d.apPass     = getSecret("apx");
  if (d.apPass.length() < 8) { d.apPass = newApPass(); putSecret("apx", d.apPass); }
  d.ideOta     = prefs.getBool("ideota", false);
  d.nightOn    = prefs.getBool("n_on", false);
  d.nightFrom  = prefs.getUShort("n_from", 22 * 60 + 30);
  d.nightTo    = prefs.getUShort("n_to", 6 * 60 + 30);
  d.nightMode  = prefs.getUChar("n_mode", 0);
  d.powerSave  = prefs.getBool("psave", true);
  // Punkte 1–5 wie auf der Testseite. Ab Werk: W1 = 3, W2 = 1, Schlummern = 2 (rechts Mitte), Flug = 5
  static const uint8_t DEF_ROLES[4] = {2, 0, 1, 4};
  if (prefs.getBytesLength("dots") == 4) prefs.getBytes("dots", d.dotRole, 4);
  else memcpy(d.dotRole, DEF_ROLES, 4);
  d.hostname   = prefs.getString("host", DEFAULT_HOSTNAME);
  d.tz         = prefs.getString("tz", DEFAULT_TZ);
  d.brightness = prefs.getUChar("bright", 2);
  d.rotate180  = prefs.getBool("rot", true);
  d.colonBlink = prefs.getBool("blink", true);
  d.sound      = prefs.getBool("sound", true);
  d.greeting   = prefs.getString("greet", "HALLO!");
  d.intro      = prefs.getBool("intro", true);
  d.keysSeen   = prefs.getBool("keys", false);
  d.updateUrl  = prefs.getString("upurl", DEFAULT_UPDATE_URL);
  d.autoUpdate = prefs.getBool("upauto", false);
  if (!prefs.isKey("hash")) {
    resetLogin();
  } else {
    d.user = prefs.getString("user", DEFAULT_USER);
    d.passSalt = prefs.getString("salt", "");
    d.passHash = prefs.getString("hash", "");
    d.hashVer = prefs.getUChar("hver", 1);
    d.otaHash = migrateSecret("ota", "otax");
    if (d.otaHash.isEmpty()) d.otaHash = md5Hex(DEFAULT_PASS);
    d.defaultLogin = prefs.getBool("deflogin", true);
  }
}

inline void saveWifi(const String &ssid, const String &pass) {
  d.wifiSsid = ssid; d.wifiPass = pass;
  prefs.putString("ssid", ssid); putSecret("wpx", pass);
}
inline void saveHostname(const String &h) { d.hostname = h; prefs.putString("host", h); }
inline void saveBrightness(uint8_t b) { d.brightness = b > 7 ? 7 : b; prefs.putUChar("bright", d.brightness); }
inline void saveRotate(bool r) { d.rotate180 = r; prefs.putBool("rot", r); }
inline void saveBlink(bool b) { d.colonBlink = b; prefs.putBool("blink", b); }
inline void saveSound(bool s) { d.sound = s; prefs.putBool("sound", s); }
inline void saveGreeting(const String &g) { d.greeting = g; prefs.putString("greet", g); }
inline void saveIntro(bool on) { d.intro = on; prefs.putBool("intro", on); }
inline void saveKeysSeen() { d.keysSeen = true; prefs.putBool("keys", true); }
inline void saveUpdate(const String &url, bool autoUp) { d.updateUrl = url; d.autoUpdate = autoUp; prefs.putString("upurl", url); prefs.putBool("upauto", autoUp); }
inline void saveTz(const String &t) { d.tz = t; prefs.putString("tz", t); }

inline void saveIdeOta(bool on) { d.ideOta = on; prefs.putBool("ideota", on); }
inline void saveNight(bool on, uint16_t from, uint16_t to, uint8_t mode) {
  d.nightOn = on; d.nightFrom = from; d.nightTo = to; d.nightMode = mode;
  prefs.putBool("n_on", on); prefs.putUShort("n_from", from); prefs.putUShort("n_to", to); prefs.putUChar("n_mode", mode);
}
inline void saveDots(const uint8_t r[4]) { memcpy(d.dotRole, r, 4); prefs.putBytes("dots", r, 4); }
inline void savePowerSave(bool on) { d.powerSave = on; prefs.putBool("psave", on); }
inline void newSetupPass() { d.apPass = newApPass(); putSecret("apx", d.apPass); }

// Alles löschen (auch Gerätegeheimnis -> neues Einrichtungs-Passwort beim nächsten Start)
inline void factoryReset() { prefs.clear(); }

}  // namespace Settings
