#pragma once
// ---------------------------------------------------------------------------
// Satellitenbahn (SGP4, erdnahe Bahnen wie die ISS) und Überflug-Vorhersage.
// Ohne Arduino-Abhängigkeiten – am PC gegen die Referenz-Implementierung getestet.
//
// SGP4 nach Hoots/Roehrich, Spacetrack Report #3, in der Fassung von Vallado
// (2006, „improved mode“, WGS-72-Konstanten). Nur der erdnahe Teil (Umlaufzeit
// unter 225 Minuten) – das reicht für die ISS.
// ---------------------------------------------------------------------------
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

namespace Sat {

constexpr double PI_ = 3.14159265358979323846;
constexpr double TWOPI = 2.0 * PI_;
constexpr double DEG = PI_ / 180.0;

// WGS-72 (wie in den TLE-Daten verwendet)
constexpr double MU = 398600.8;
constexpr double RE = 6378.135;
constexpr double J2 = 0.001082616;
constexpr double J3 = -0.00000253881;
constexpr double J4 = -0.00000165597;
constexpr double J3OJ2 = J3 / J2;
inline double xke() { return 60.0 / sqrt(RE * RE * RE / MU); }

inline double jdFromUnix(double unixSeconds) { return unixSeconds / 86400.0 + 2440587.5; }

// Julianisches Datum zu Jahresbeginn (1. Januar, 0 Uhr UT)
inline double jdJan1(int year) {
  int y = year - 1, a = y / 100, b = 2 - a + a / 4;
  return floor(365.25 * (y + 4716)) + floor(30.6001 * 14) + 1 + b - 1524.5;
}

// Sternzeit Greenwich (rad)
inline double gmst(double jdut1) {
  double t = (jdut1 - 2451545.0) / 36525.0;
  double temp = -6.2e-6 * t * t * t + 0.093104 * t * t + (876600.0 * 3600 + 8640184.812866) * t + 67310.54841;
  temp = fmod(temp * DEG / 240.0, TWOPI);
  if (temp < 0) temp += TWOPI;
  return temp;
}

// ---- TLE ----------------------------------------------------------------------
struct Tle {
  double epochJd = 0, bstar = 0, inclo = 0, nodeo = 0, ecco = 0, argpo = 0, mo = 0, noKozai = 0;
  bool ok = false;
};

inline double field(const char *s, int from, int to) {   // Spalten 1-basiert, einschließlich
  char b[24]; int n = to - from + 1; if (n > 23) n = 23;
  memcpy(b, s + from - 1, n); b[n] = 0;
  return atof(b);
}
// Format " 12345-4" = 0.12345e-4
inline double expField(const char *s, int from) {
  char b[16]; memcpy(b, s + from - 1, 8); b[8] = 0;
  char m[12]; int k = 0;
  const char *p = b;
  while (*p == ' ') p++;
  bool neg = false;
  if (*p == '-' || *p == '+') { neg = *p == '-'; p++; }
  m[k++] = '0'; m[k++] = '.';
  while (*p >= '0' && *p <= '9' && k < 10) m[k++] = *p++;
  m[k] = 0;
  double mant = atof(m);
  int ex = atoi(p);
  double v = mant * pow(10.0, ex);
  return neg ? -v : v;
}

inline bool checksumOk(const char *l) {
  if (strlen(l) < 69) return false;
  int sum = 0;
  for (int i = 0; i < 68; i++) { char c = l[i]; if (c >= '0' && c <= '9') sum += c - '0'; else if (c == '-') sum += 1; }
  return (sum % 10) == (l[68] - '0');
}

inline bool parseTle(const char *l1, const char *l2, Tle &t) {
  t.ok = false;
  if (!l1 || !l2 || l1[0] != '1' || l2[0] != '2' || strlen(l1) < 69 || strlen(l2) < 69) return false;
  if (!checksumOk(l1) || !checksumOk(l2)) return false;
  int yy = (int)field(l1, 19, 20);
  double day = field(l1, 21, 32);
  int year = yy < 57 ? 2000 + yy : 1900 + yy;
  t.epochJd = jdJan1(year) + day - 1.0;
  t.bstar = expField(l1, 54);
  t.inclo = field(l2, 9, 16) * DEG;
  t.nodeo = field(l2, 18, 25) * DEG;
  char e[12] = "0."; memcpy(e + 2, l2 + 26, 7); e[9] = 0;
  t.ecco = atof(e);
  t.argpo = field(l2, 35, 42) * DEG;
  t.mo = field(l2, 44, 51) * DEG;
  t.noKozai = field(l2, 53, 63) * TWOPI / 1440.0;   // rad/min
  t.ok = t.noKozai > 0;
  return t.ok;
}

// ---- SGP4 (erdnah) -------------------------------------------------------------------
struct Sgp4 {
  Tle e;
  bool ok = false;
  int isimp = 0;
  double no, con41, x1mth2, x7thm1, cc1, cc4, cc5, d2, d3, d4, delmo, eta, argpdot, omgcof, sinmao,
         t2cof, t3cof, t4cof, t5cof, xlcof, aycof, mdot, nodedot, nodecf, xmcof;

  bool init(const Tle &t) {
    ok = false; e = t;
    if (!t.ok) return false;
    const double x2o3 = 2.0 / 3.0, XKE = xke();
    double ecco = t.ecco, inclo = t.inclo;
    // initl
    double eccsq = ecco * ecco, omeosq = 1.0 - eccsq, rteosq = sqrt(omeosq);
    double cosio = cos(inclo), cosio2 = cosio * cosio;
    double ak = pow(XKE / t.noKozai, x2o3);
    double d1 = 0.75 * J2 * (3.0 * cosio2 - 1.0) / (rteosq * omeosq);
    double del = d1 / (ak * ak);
    double adel = ak * (1.0 - del * del - del * (1.0 / 3.0 + 134.0 * del * del / 81.0));
    del = d1 / (adel * adel);
    no = t.noKozai / (1.0 + del);
    if (TWOPI / no >= 225.0) return false;               // keine erdnahe Bahn
    double ao = pow(XKE / no, x2o3);
    double sinio = sin(inclo), po = ao * omeosq;
    double con42 = 1.0 - 5.0 * cosio2;
    con41 = -con42 - cosio2 - cosio2;
    double posq = po * po, rp = ao * (1.0 - ecco);
    // sgp4init
    double ss = 78.0 / RE + 1.0;
    double qzms2t = pow((120.0 - 78.0) / RE, 4);
    isimp = rp < 220.0 / RE + 1.0 ? 1 : 0;
    double sfour = ss, qzms24 = qzms2t;
    double perige = (rp - 1.0) * RE;
    if (perige < 156.0) {
      sfour = perige - 78.0;
      if (perige < 98.0) sfour = 20.0;
      qzms24 = pow((120.0 - sfour) / RE, 4);
      sfour = sfour / RE + 1.0;
    }
    double pinvsq = 1.0 / posq, tsi = 1.0 / (ao - sfour);
    eta = ao * ecco * tsi;
    double etasq = eta * eta, eeta = ecco * eta, psisq = fabs(1.0 - etasq);
    double coef = qzms24 * pow(tsi, 4.0), coef1 = coef / pow(psisq, 3.5);
    double cc2 = coef1 * no * (ao * (1.0 + 1.5 * etasq + eeta * (4.0 + etasq)) +
                 0.375 * J2 * tsi / psisq * con41 * (8.0 + 3.0 * etasq * (8.0 + etasq)));
    cc1 = t.bstar * cc2;
    double cc3 = ecco > 1.0e-4 ? -2.0 * coef * tsi * J3OJ2 * no * sinio / ecco : 0.0;
    x1mth2 = 1.0 - cosio2;
    cc4 = 2.0 * no * coef1 * ao * omeosq *
          (eta * (2.0 + 0.5 * etasq) + ecco * (0.5 + 2.0 * etasq) -
           J2 * tsi / (ao * psisq) * (-3.0 * con41 * (1.0 - 2.0 * eeta + etasq * (1.5 - 0.5 * eeta)) +
           0.75 * x1mth2 * (2.0 * etasq - eeta * (1.0 + etasq)) * cos(2.0 * t.argpo)));
    cc5 = 2.0 * coef1 * ao * omeosq * (1.0 + 2.75 * (etasq + eeta) + eeta * etasq);
    double cosio4 = cosio2 * cosio2;
    double temp1 = 1.5 * J2 * pinvsq * no, temp2 = 0.5 * temp1 * J2 * pinvsq, temp3 = -0.46875 * J4 * pinvsq * pinvsq * no;
    mdot = no + 0.5 * temp1 * rteosq * con41 + 0.0625 * temp2 * rteosq * (13.0 - 78.0 * cosio2 + 137.0 * cosio4);
    argpdot = -0.5 * temp1 * con42 + 0.0625 * temp2 * (7.0 - 114.0 * cosio2 + 395.0 * cosio4) + temp3 * (3.0 - 36.0 * cosio2 + 49.0 * cosio4);
    double xhdot1 = -temp1 * cosio;
    nodedot = xhdot1 + (0.5 * temp2 * (4.0 - 19.0 * cosio2) + 2.0 * temp3 * (3.0 - 7.0 * cosio2)) * cosio;
    omgcof = t.bstar * cc3 * cos(t.argpo);
    xmcof = ecco > 1.0e-4 ? -x2o3 * coef * t.bstar / eeta : 0.0;
    nodecf = 3.5 * omeosq * xhdot1 * cc1;
    t2cof = 1.5 * cc1;
    xlcof = fabs(cosio + 1.0) > 1.5e-12 ? -0.25 * J3OJ2 * sinio * (3.0 + 5.0 * cosio) / (1.0 + cosio)
                                       : -0.25 * J3OJ2 * sinio * (3.0 + 5.0 * cosio) / 1.5e-12;
    aycof = -0.5 * J3OJ2 * sinio;
    double dm = 1.0 + eta * cos(t.mo);
    delmo = dm * dm * dm;
    sinmao = sin(t.mo);
    x7thm1 = 7.0 * cosio2 - 1.0;
    d2 = d3 = d4 = t3cof = t4cof = t5cof = 0;
    if (!isimp) {
      double cc1sq = cc1 * cc1;
      d2 = 4.0 * ao * tsi * cc1sq;
      double temp = d2 * tsi * cc1 / 3.0;
      d3 = (17.0 * ao + sfour) * temp;
      d4 = 0.5 * temp * ao * tsi * (221.0 * ao + 31.0 * sfour) * cc1;
      t3cof = d2 + 2.0 * cc1sq;
      t4cof = 0.25 * (3.0 * d3 + cc1 * (12.0 * d2 + 10.0 * cc1sq));
      t5cof = 0.2 * (3.0 * d4 + 12.0 * cc1 * d3 + 6.0 * d2 * d2 + 15.0 * cc1sq * (2.0 * d2 + cc1sq));
    }
    ok = true;
    return true;
  }

  // Position (km, TEME) tsince Minuten nach der Epoche
  bool propagate(double t, double r[3]) const {
    if (!ok) return false;
    const double x2o3 = 2.0 / 3.0, XKE = xke();
    double xmdf = e.mo + mdot * t;
    double argpdf = e.argpo + argpdot * t;
    double nodedf = e.nodeo + nodedot * t;
    double argpm = argpdf, mm = xmdf, t2 = t * t;
    double nodem = nodedf + nodecf * t2;
    double tempa = 1.0 - cc1 * t, tempe = e.bstar * cc4 * t, templ = t2cof * t2;
    if (!isimp) {
      double delomg = omgcof * t;
      double dmt = 1.0 + eta * cos(xmdf);
      double delm = xmcof * (dmt * dmt * dmt - delmo);
      double temp = delomg + delm;
      mm = xmdf + temp;
      argpm = argpdf - temp;
      double t3 = t2 * t, t4 = t3 * t;
      tempa = tempa - d2 * t2 - d3 * t3 - d4 * t4;
      tempe = tempe + e.bstar * cc5 * (sin(mm) - sinmao);
      templ = templ + t3cof * t3 + t4 * (t4cof + t * t5cof);
    }
    double nm = no, em = e.ecco, inclm = e.inclo;
    double am = pow(XKE / nm, x2o3) * tempa * tempa;
    nm = XKE / pow(am, 1.5);
    em = em - tempe;
    if (em >= 1.0 || em < -0.001 || am < 0.95) return false;
    if (em < 1.0e-6) em = 1.0e-6;
    mm = mm + no * templ;
    double xlm = mm + argpm + nodem;
    nodem = fmod(nodem, TWOPI);
    argpm = fmod(argpm, TWOPI);
    xlm = fmod(xlm, TWOPI);
    mm = fmod(xlm - argpm - nodem, TWOPI);
    double sinim = sin(inclm), cosim = cos(inclm);
    double ep = em, xincp = inclm, argpp = argpm, nodep = nodem, mp = mm, sinip = sinim, cosip = cosim;
    double axnl = ep * cos(argpp);
    double temp = 1.0 / (am * (1.0 - ep * ep));
    double aynl = ep * sin(argpp) + temp * aycof;
    double xl = mp + argpp + nodep + temp * xlcof * axnl;
    double u = fmod(xl - nodep, TWOPI);
    double eo1 = u, tem5 = 9999.9, sineo1 = 0, coseo1 = 0;
    int ktr = 1;
    while (fabs(tem5) >= 1.0e-12 && ktr <= 10) {
      sineo1 = sin(eo1); coseo1 = cos(eo1);
      tem5 = 1.0 - coseo1 * axnl - sineo1 * aynl;
      tem5 = (u - aynl * coseo1 + axnl * sineo1 - eo1) / tem5;
      if (fabs(tem5) >= 0.95) tem5 = tem5 > 0.0 ? 0.95 : -0.95;
      eo1 = eo1 + tem5;
      ktr++;
    }
    double ecose = axnl * coseo1 + aynl * sineo1;
    double esine = axnl * sineo1 - aynl * coseo1;
    double el2 = axnl * axnl + aynl * aynl;
    double pl = am * (1.0 - el2);
    if (pl < 0.0) return false;
    double rl = am * (1.0 - ecose);
    double betal = sqrt(1.0 - el2);
    temp = esine / (1.0 + betal);
    double sinu = am / rl * (sineo1 - aynl - axnl * temp);
    double cosu = am / rl * (coseo1 - axnl + aynl * temp);
    double su = atan2(sinu, cosu);
    double sin2u = (cosu + cosu) * sinu;
    double cos2u = 1.0 - 2.0 * sinu * sinu;
    temp = 1.0 / pl;
    double temp1 = 0.5 * J2 * temp, temp2 = temp1 * temp;
    double mrt = rl * (1.0 - 1.5 * temp2 * betal * con41) + 0.5 * temp1 * x1mth2 * cos2u;
    su = su - 0.25 * temp2 * x7thm1 * sin2u;
    double xnode = nodep + 1.5 * temp2 * cosip * sin2u;
    double xinc = xincp + 1.5 * temp2 * cosip * sinip * cos2u;
    double sinsu = sin(su), cossu = cos(su), snod = sin(xnode), cnod = cos(xnode), sini = sin(xinc), cosi = cos(xinc);
    double xmx = -snod * cosi, xmy = cnod * cosi;
    double ux = xmx * sinsu + cnod * cossu, uy = xmy * sinsu + snod * cossu, uz = sini * sinsu;
    if (mrt < 1.0) return false;     // unter der Erdoberfläche
    r[0] = mrt * ux * RE; r[1] = mrt * uy * RE; r[2] = mrt * uz * RE;
    return true;
  }

  bool positionAt(double jd, double r[3]) const { return propagate((jd - e.epochJd) * 1440.0, r); }
};

// ---- Beobachter, Blickrichtung, Sonne ------------------------------------------------------
struct Observer {
  double lat, lon, altKm;     // rad, rad, km
  double ecef[3];
  void set(double latDeg, double lonDeg, double altM = 50) {
    lat = latDeg * DEG; lon = lonDeg * DEG; altKm = altM / 1000.0;
    const double a = 6378.137, f = 1 / 298.257223563, e2 = f * (2 - f);
    double s = sin(lat), n = a / sqrt(1 - e2 * s * s);
    ecef[0] = (n + altKm) * cos(lat) * cos(lon);
    ecef[1] = (n + altKm) * cos(lat) * sin(lon);
    ecef[2] = (n * (1 - e2) + altKm) * s;
  }
};

// Richtung (Azimut 0 = Nord, im Uhrzeigersinn) und Höhe über dem Horizont, Grad
inline void lookAngles(const double rTeme[3], double jd, const Observer &o, double &azDeg, double &elDeg) {
  double g = gmst(jd), cg = cos(g), sg = sin(g);
  double x = cg * rTeme[0] + sg * rTeme[1];
  double y = -sg * rTeme[0] + cg * rTeme[1];
  double z = rTeme[2];
  double rx = x - o.ecef[0], ry = y - o.ecef[1], rz = z - o.ecef[2];
  double sl = sin(o.lat), cl = cos(o.lat), so = sin(o.lon), co = cos(o.lon);
  double south = sl * co * rx + sl * so * ry - cl * rz;
  double east = -so * rx + co * ry;
  double zen = cl * co * rx + cl * so * ry + sl * rz;
  double range = sqrt(rx * rx + ry * ry + rz * rz);
  elDeg = asin(zen / range) / DEG;
  double az = atan2(east, -south) / DEG;
  azDeg = az < 0 ? az + 360 : az;
}

// Richtung zur Sonne (Einheitsvektor, äquatorial) – genau genug für Tag/Nacht und Erdschatten
inline void sunDir(double jd, double s[3]) {
  double n = jd - 2451545.0;
  double L = fmod(280.460 + 0.9856474 * n, 360.0) * DEG;
  double g = fmod(357.528 + 0.9856003 * n, 360.0) * DEG;
  double lam = L + (1.915 * sin(g) + 0.020 * sin(2 * g)) * DEG;
  double eps = (23.439 - 0.0000004 * n) * DEG;
  s[0] = cos(lam); s[1] = cos(eps) * sin(lam); s[2] = sin(eps) * sin(lam);
}

inline double sunElevation(double jd, const Observer &o) {
  double s[3]; sunDir(jd, s);
  double far[3] = {s[0] * 1.496e8, s[1] * 1.496e8, s[2] * 1.496e8};
  double az, el; lookAngles(far, jd, o, az, el);
  return el;
}

// Wird der Satellit von der Sonne beschienen? (zylindrischer Erdschatten)
inline bool sunlit(const double r[3], double jd) {
  double s[3]; sunDir(jd, s);
  double d = r[0] * s[0] + r[1] * s[1] + r[2] * s[2];
  if (d > 0) return true;
  double px = r[0] - d * s[0], py = r[1] - d * s[1], pz = r[2] - d * s[2];
  return sqrt(px * px + py * py + pz * pz) > 6378.137;
}

// ---- Überflüge -----------------------------------------------------------------------------
struct Pass {
  int64_t start, end, peak;   // Unix-Zeit (UTC)
  float maxEl, azStart, azEnd, azPeak;
  bool visible;               // dunkler Himmel beim Beobachter UND ISS im Sonnenlicht
};

typedef void (*YieldFn)();

inline bool elevation(const Sgp4 &s, const Observer &o, double unixT, double &az, double &el) {
  double r[3];
  double jd = jdFromUnix(unixT);
  if (!s.positionAt(jd, r)) return false;
  lookAngles(r, jd, o, az, el);
  return true;
}

// Zeitpunkt, an dem die Höhe die Schwelle kreuzt (zwischen a und b), auf 1 s genau
inline double refineCross(const Sgp4 &s, const Observer &o, double a, double b, double thr, bool rising) {
  for (int i = 0; i < 20 && b - a > 1.0; i++) {
    double m = (a + b) / 2, az, el;
    elevation(s, o, m, az, el);
    if ((el >= thr) == rising) b = m; else a = m;
  }
  return rising ? b : a;
}

// Überflüge mit Höhe >= horizonDeg suchen (Beginn/Ende bei horizonDeg), nur solche mit
// höchstem Punkt >= minMaxEl. Rückgabe: Anzahl.
inline int findPasses(const Sgp4 &s, const Observer &o, int64_t fromUnix, double hours, double horizonDeg,
                      double minMaxEl, bool visibleOnly, Pass *out, int maxOut, YieldFn yieldFn = nullptr) {
  int n = 0;
  const double step = 30.0;
  double t = (double)fromUnix - 900.0;          // läuft vielleicht gerade einer
  double end = (double)fromUnix + hours * 3600.0;
  double az, el, prevT = t;
  if (!elevation(s, o, t, az, el)) return 0;
  bool up = el >= horizonDeg;
  Pass cur{}; bool vis = false;
  if (up) { cur.start = (int64_t)t; cur.azStart = (float)az; cur.maxEl = (float)el; cur.peak = (int64_t)t; cur.azPeak = (float)az; vis = false; }
  int cnt = 0;
  while (t < end && n < maxOut) {
    prevT = t; t += step;
    if (!elevation(s, o, t, az, el)) return n;
    if (yieldFn && (++cnt & 31) == 0) yieldFn();
    if (!up && el >= horizonDeg) {
      up = true;
      double ts = refineCross(s, o, prevT, t, horizonDeg, true);
      double a2, e2; elevation(s, o, ts, a2, e2);
      cur = Pass{}; cur.start = (int64_t)ts; cur.azStart = (float)a2; cur.maxEl = (float)el; cur.peak = (int64_t)t; cur.azPeak = (float)az;
      vis = false;
    }
    if (up) {
      if (el > cur.maxEl) { cur.maxEl = (float)el; cur.peak = (int64_t)t; cur.azPeak = (float)az; }
      if (!vis) {
        double jd = jdFromUnix(t), r[3];
        if (s.positionAt(jd, r) && sunlit(r, jd) && sunElevation(jd, o) < -6.0) vis = true;
      }
      if (el < horizonDeg) {
        up = false;
        double te = refineCross(s, o, prevT, t, horizonDeg, false);
        double a2, e2; elevation(s, o, te, a2, e2);
        cur.end = (int64_t)te; cur.azEnd = (float)a2; cur.visible = vis;
        // höchsten Punkt genauer bestimmen (Dreiteilungs-Suche ±30 s)
        double lo = cur.peak - 30.0, hi = cur.peak + 30.0;
        for (int i = 0; i < 25 && hi - lo > 0.5; i++) {
          double m1 = lo + (hi - lo) / 3, m2 = hi - (hi - lo) / 3, a1, e1, a2b, e2b;
          elevation(s, o, m1, a1, e1); elevation(s, o, m2, a2b, e2b);
          if (e1 < e2b) lo = m1; else hi = m2;
        }
        double ap, epk; elevation(s, o, (lo + hi) / 2, ap, epk);
        if (epk > cur.maxEl) { cur.maxEl = (float)epk; cur.azPeak = (float)ap; cur.peak = (int64_t)((lo + hi) / 2); }
        if (cur.end > fromUnix && cur.maxEl >= minMaxEl && (!visibleOnly || cur.visible)) out[n++] = cur;
      }
    }
  }
  return n;
}

inline const char *compass8(double az) {
  static const char *N[] = {"N", "NO", "O", "SO", "S", "SW", "W", "NW"};
  int i = (int)((az + 22.5) / 45.0) % 8;
  return N[i < 0 ? 0 : i];
}

}  // namespace Sat
