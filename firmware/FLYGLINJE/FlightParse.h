#pragma once
// ---------------------------------------------------------------------------
// Flugdaten zerlegen – ohne Arduino-Abhängigkeiten (auch am PC testbar).
// Liest die Antwort von adsb.lol / airplanes.live Zeichen für Zeichen und
// holt aus jedem Flugzeug-Objekt nur die Felder, die die Uhr braucht.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <stdio.h>

namespace FlightParse {

struct Plane {
  char hex[7];
  char call[9];       // Flugnummer/Rufzeichen, ohne Leerzeichen
  char type[5];       // ICAO-Typ, z. B. A320
  char reg[10];       // Kennzeichen
  int32_t altFt;      // barometrische Höhe in Fuß
  bool ground;
  int16_t gsKmh;      // Geschwindigkeit über Grund
  int16_t rateFpm;    // Steig-/Sinkrate
  uint16_t squawk;
  float lat, lon;
  float distKm;
  int16_t bearing;    // Richtung vom Standort aus, 0 = Nord
  int8_t elev;        // Höhenwinkel über dem Horizont in Grad
  char from[5], to[5];   // Route (IATA), leer = unbekannt
  bool routeChecked;
  uint8_t dbFlags;    // adsb.lol: 1 = Militär, 2 = interessant, 4 = PIA, 8 = LADD
  uint8_t emerg;      // gemeldeter Notfall, siehe EMERG_*
  char cat[3];        // Kategorie, z. B. A3 (Verkehrsflugzeug), A7 (Hubschrauber)
};

// Notfallart aus dem Feld "emergency" (ADS-B) – zusätzlich zum Squawk
enum Emerg : uint8_t { EMERG_NONE, EMERG_GENERAL, EMERG_LIFEGUARD, EMERG_MINFUEL, EMERG_NORDO, EMERG_UNLAWFUL, EMERG_DOWNED };
inline uint8_t emergFrom(const char *v) {
  if (!strcmp(v, "general")) return EMERG_GENERAL;
  if (!strcmp(v, "lifeguard")) return EMERG_LIFEGUARD;
  if (!strcmp(v, "minfuel")) return EMERG_MINFUEL;
  if (!strcmp(v, "nordo")) return EMERG_NORDO;
  if (!strcmp(v, "unlawful")) return EMERG_UNLAWFUL;
  if (!strcmp(v, "downed")) return EMERG_DOWNED;
  return EMERG_NONE;
}

// Wert eines Schlüssels aus einem flachen JSON-Objekt als Text (ohne Anführungszeichen)
inline bool get(const char *obj, const char *key, char *out, size_t n) {
  char pat[24];
  size_t kl = strlen(key);
  if (kl + 3 > sizeof(pat)) return false;
  pat[0] = '"'; memcpy(pat + 1, key, kl); pat[kl + 1] = '"'; pat[kl + 2] = 0;
  const char *p = obj;
  while ((p = strstr(p, pat)) != nullptr) {
    const char *q = p + kl + 2;
    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
    if (*q != ':') { p += kl + 2; continue; }
    q++;
    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r') q++;
    size_t o = 0;
    if (*q == '"') {
      q++;
      while (*q && *q != '"' && o + 1 < n) {
        if (*q == '\\' && q[1]) q++;
        out[o++] = *q++;
      }
    } else {
      while (*q && *q != ',' && *q != '}' && *q != ']' && *q != ' ' && o + 1 < n) out[o++] = *q++;
    }
    out[o] = 0;
    return true;
  }
  return false;
}

inline void trimCopy(char *dst, size_t n, const char *src) {
  while (*src == ' ') src++;
  size_t l = strlen(src);
  while (l && src[l - 1] == ' ') l--;
  if (l >= n) l = n - 1;
  memcpy(dst, src, l); dst[l] = 0;
}

constexpr double DEG = 3.14159265358979323846 / 180.0;

inline double distanceKm(double lat1, double lon1, double lat2, double lon2) {
  double dLat = (lat2 - lat1) * DEG, dLon = (lon2 - lon1) * DEG;
  double a = sin(dLat / 2) * sin(dLat / 2) + cos(lat1 * DEG) * cos(lat2 * DEG) * sin(dLon / 2) * sin(dLon / 2);
  return 6371.0 * 2 * atan2(sqrt(a), sqrt(1 - a));
}

inline int bearingDeg(double lat1, double lon1, double lat2, double lon2) {
  double y = sin((lon2 - lon1) * DEG) * cos(lat2 * DEG);
  double x = cos(lat1 * DEG) * sin(lat2 * DEG) - sin(lat1 * DEG) * cos(lat2 * DEG) * cos((lon2 - lon1) * DEG);
  int b = (int)lround(atan2(y, x) / DEG);
  return (b + 360) % 360;
}

// Ein Objekt auswerten. false = unbrauchbar (keine Position o. Ä.)
inline bool parsePlane(const char *obj, double homeLat, double homeLon, Plane &p) {
  char v[24];
  memset(&p, 0, sizeof(p));
  if (!get(obj, "lat", v, sizeof(v))) return false;
  p.lat = (float)atof(v);
  if (!get(obj, "lon", v, sizeof(v))) return false;
  p.lon = (float)atof(v);
  if (get(obj, "hex", v, sizeof(v))) trimCopy(p.hex, sizeof(p.hex), v[0] == '~' ? v + 1 : v);
  if (get(obj, "flight", v, sizeof(v))) trimCopy(p.call, sizeof(p.call), v);
  if (get(obj, "t", v, sizeof(v))) trimCopy(p.type, sizeof(p.type), v);
  if (get(obj, "r", v, sizeof(v))) trimCopy(p.reg, sizeof(p.reg), v);
  if (get(obj, "alt_baro", v, sizeof(v))) {
    if (strcmp(v, "ground") == 0) p.ground = true; else p.altFt = atol(v);
  } else if (get(obj, "alt_geom", v, sizeof(v))) {
    p.altFt = atol(v);
  }
  if (get(obj, "gs", v, sizeof(v))) p.gsKmh = (int16_t)lround(atof(v) * 1.852);
  if (get(obj, "baro_rate", v, sizeof(v)) || get(obj, "geom_rate", v, sizeof(v))) p.rateFpm = (int16_t)atoi(v);
  if (get(obj, "squawk", v, sizeof(v))) p.squawk = (uint16_t)atoi(v);
  if (get(obj, "dbFlags", v, sizeof(v))) p.dbFlags = (uint8_t)atoi(v);
  if (get(obj, "emergency", v, sizeof(v))) p.emerg = emergFrom(v);
  if (get(obj, "category", v, sizeof(v))) trimCopy(p.cat, sizeof(p.cat), v);
  double d = distanceKm(homeLat, homeLon, p.lat, p.lon);
  p.distKm = (float)d;
  p.bearing = (int16_t)bearingDeg(homeLat, homeLon, p.lat, p.lon);
  double altM = p.ground ? 0 : p.altFt * 0.3048;
  double e = atan2(altM, d * 1000.0) / DEG;
  p.elev = (int8_t)lround(e > 90 ? 90 : e);
  return true;
}

// Zustandsautomat: bekommt die Antwort Zeichen für Zeichen und meldet jedes
// vollständige Objekt im Array "ac".
struct Stream {
  enum Phase { FIND_KEY, FIND_ARRAY, IN_ARRAY, DONE };
  Phase phase = FIND_KEY;
  const char *key = "\"ac\"";
  size_t keyPos = 0;
  int depth = 0;
  bool inString = false, escape = false, overflow = false;
  char buf[1800];
  size_t len = 0;

  void reset() { phase = FIND_KEY; keyPos = 0; depth = 0; inString = escape = overflow = false; len = 0; }

  // gibt true zurück, wenn in buf ein fertiges Objekt steht
  bool feed(char c) {
    switch (phase) {
      case FIND_KEY:
        if (c == key[keyPos]) { if (!key[++keyPos]) phase = FIND_ARRAY; }
        else keyPos = (c == key[0]) ? 1 : 0;
        return false;
      case FIND_ARRAY:
        if (c == '[') { phase = IN_ARRAY; depth = 0; }
        return false;
      case IN_ARRAY:
        if (depth == 0) {
          if (c == '{') { depth = 1; len = 0; overflow = false; buf[len++] = c; }
          else if (c == ']') phase = DONE;
          return false;
        }
        if (len + 1 < sizeof(buf)) buf[len++] = c; else overflow = true;
        if (inString) {
          if (escape) escape = false;
          else if (c == '\\') escape = true;
          else if (c == '"') inString = false;
          return false;
        }
        if (c == '"') inString = true;
        else if (c == '{') depth++;
        else if (c == '}') {
          if (--depth == 0) { buf[len] = 0; return !overflow; }
        }
        return false;
      default:
        return false;
    }
  }
};

// ---- Routen von adsb.lol (POST /api/0/routeset) -------------------------------------------
// Antwort: [{"callsign":"EWG9AT","airport_codes":"EDDL-LEPA","_airport_codes_iata":"DUS-PMI",
//            "_airports":[{..."iata":"DUS","lat":51.28,"lon":6.76...},...],"plausible":true}, ...]
// "plausible" = die aktuelle Position passt zu (einem Abschnitt) der Route – so zeigt es auch die
// Karte auf adsb.lol. Unpassende oder unbekannte Routen werden als "keine Route" gemeldet.
constexpr int MAX_LEGS = 4;
struct RouteInfo {
  char call[9];
  int airports;               // 0 = keine Route bekannt
  char code[MAX_LEGS][5];     // IATA (sonst ICAO, gekürzt)
  float lat[MAX_LEGS], lon[MAX_LEGS];
  bool plausible;
};

// Ende eines Objekts/Arrays ab p (p zeigt auf '{' oder '['), Strings werden beachtet
inline char *blockEnd(char *p) {
  int depth = 0; bool str = false, esc = false;
  for (; *p; p++) {
    char c = *p;
    if (str) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') str = false; continue; }
    if (c == '"') str = true;
    else if (c == '{' || c == '[') depth++;
    else if (c == '}' || c == ']') { if (--depth == 0) return p; }
  }
  return nullptr;
}

typedef void (*RouteCb)(const RouteInfo &r, void *user);

// buf wird beim Auswerten vorübergehend verändert (Endmarken), danach wiederhergestellt
inline int parseRouteSet(char *buf, RouteCb cb, void *user) {
  char *p = strchr(buf, '[');
  if (!p) return 0;
  p++;
  int n = 0;
  while ((p = strchr(p, '{')) != nullptr) {
    char *e = blockEnd(p);
    if (!e) break;
    char saved = e[1]; e[1] = 0;
    RouteInfo r; memset(&r, 0, sizeof(r));
    char v[48];
    if (get(p, "callsign", v, sizeof(v))) trimCopy(r.call, sizeof(r.call), v);
    r.plausible = get(p, "plausible", v, sizeof(v)) && (strcmp(v, "true") == 0 || strcmp(v, "1") == 0);
    // Flughafen-Kürzel aus "_airport_codes_iata" (z. B. "DUS-PMI"), "unknown" = keine Route
    if (get(p, "_airport_codes_iata", v, sizeof(v)) && strcmp(v, "unknown") != 0 && v[0]) {
      char *s = v;
      while (*s && r.airports < MAX_LEGS) {
        char *d = strchr(s, '-');
        size_t l = d ? (size_t)(d - s) : strlen(s);
        if (l > 4) l = 4;
        memcpy(r.code[r.airports], s, l); r.code[r.airports][l] = 0;
        r.airports++;
        if (!d) break;
        s = d + 1;
      }
    }
    // Koordinaten der Flughäfen (gleiche Reihenfolge)
    char *ap = strstr(p, "\"_airports\"");
    if (ap) ap = strchr(ap, '[');
    if (ap) {
      char *ae = blockEnd(ap);
      int k = 0;
      char *q = ap + 1;
      while (ae && k < r.airports && (q = strchr(q, '{')) != nullptr && q < ae) {
        char *qe = blockEnd(q);
        if (!qe) break;
        char sv = qe[1]; qe[1] = 0;
        char w[24];
        if (get(q, "lat", w, sizeof(w))) r.lat[k] = (float)atof(w);
        if (get(q, "lon", w, sizeof(w))) r.lon[k] = (float)atof(w);
        qe[1] = sv;
        k++; q = qe + 1;
      }
    }
    e[1] = saved;
    if (r.call[0]) { cb(r, user); n++; }
    p = e + 1;
  }
  return n;
}

// Route aus der adsbdb-Antwort: "origin"/"destination" → iata_code (sonst icao_code).
// Auch nur Start oder nur Ziel ist möglich. Rückgabe: true, wenn mindestens eins bekannt.
inline void airportCode(const char *body, const char *key, char *out) {
  out[0] = 0;
  char pat[20]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char *k = strstr(body, pat);
  if (!k) return;
  k += strlen(pat);
  while (*k == ' ' || *k == ':' || *k == '\n' || *k == '\r' || *k == '\t') k++;
  if (*k != '{') return;                           // null oder fehlt
  char obj[600];
  size_t n = 0; int depth = 0; bool str = false, esc = false;
  for (const char *q = k; *q && n + 1 < sizeof(obj); q++) {
    char c = *q; obj[n++] = c;
    if (str) { if (esc) esc = false; else if (c == '\\') esc = true; else if (c == '"') str = false; continue; }
    if (c == '"') str = true; else if (c == '{') depth++; else if (c == '}' && --depth == 0) break;
  }
  obj[n] = 0;
  char v[8];
  if (get(obj, "iata_code", v, sizeof(v)) && v[0] && strcmp(v, "null")) trimCopy(out, 5, v);
  else if (get(obj, "icao_code", v, sizeof(v)) && v[0] && strcmp(v, "null")) trimCopy(out, 5, v);
}
inline bool parseRoute(const char *body, char *from, char *to) {
  airportCode(body, "origin", from);
  airportCode(body, "destination", to);
  return from[0] || to[0];
}

// Welcher Abschnitt einer Route passt zur Position? (kleinster Umweg über die Position)
inline int bestLeg(const RouteInfo &r, double lat, double lon) {
  if (r.airports < 2) return -1;
  int best = 0; double bestX = 1e18;
  for (int i = 0; i + 1 < r.airports; i++) {
    double ab = distanceKm(r.lat[i], r.lon[i], r.lat[i + 1], r.lon[i + 1]);
    double x = distanceKm(r.lat[i], r.lon[i], lat, lon) + distanceKm(lat, lon, r.lat[i + 1], r.lon[i + 1]) - ab;
    if (x < bestX) { bestX = x; best = i; }
  }
  return best;
}

inline const char *compass(int b) {
  static const char *N[] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
  return N[((b + 22) % 360) / 45];
}

}  // namespace FlightParse
