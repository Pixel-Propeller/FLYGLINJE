#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "Net.h"
#include "FlightParse.h"
#include "Badges.h"

// ---------------------------------------------------------------------------
// Sammelalbum (nach dem Vorbild von Skycards): jeder Flugzeugtyp, jede Airline und
// jeder Flughafen, der im Radius auftaucht, landet mit Datum im Album. Gespeichert im
// sonst ungenutzten 128-KB-Bereich (LittleFS):
//   /types.bin     Typen      (40 Byte je Typ, mit Kennzeichen beim ersten Mal fürs Foto)
//   /airlines.bin  Airlines   (28 Byte je Airline)
//   /airports.bin  Flughäfen  (20 Byte je Flughafen)
// Geschrieben wird gesammelt alle 10 Minuten aus dem Hintergrund-Task.
// ---------------------------------------------------------------------------
namespace Album {

// ---- Katalog: Typen mit Namen und Seltenheit (0 gewöhnlich … 4 legendär) ----------------
struct TypeDef { const char *code; const char *name; uint8_t rar; };
inline const TypeDef TYPES[] = {
  // Airbus
  {"A318", "Airbus A318", 1}, {"A319", "Airbus A319", 0}, {"A320", "Airbus A320", 0}, {"A321", "Airbus A321", 0},
  {"A19N", "Airbus A319neo", 1}, {"A20N", "Airbus A320neo", 0}, {"A21N", "Airbus A321neo", 0},
  {"BCS1", "Airbus A220-100", 1}, {"BCS3", "Airbus A220-300", 1},
  {"A306", "Airbus A300-600", 2}, {"A310", "Airbus A310", 2},
  {"A332", "Airbus A330-200", 1}, {"A333", "Airbus A330-300", 1}, {"A338", "Airbus A330-800neo", 2}, {"A339", "Airbus A330-900neo", 1},
  {"A343", "Airbus A340-300", 2}, {"A346", "Airbus A340-600", 2},
  {"A359", "Airbus A350-900", 1}, {"A35K", "Airbus A350-1000", 2},
  {"A388", "Airbus A380", 3}, {"A400", "Airbus A400M Atlas", 2},
  {"A3ST", "Airbus Beluga", 4}, {"A337", "Airbus BelugaXL", 4},
  // Boeing
  {"B712", "Boeing 717", 2}, {"B733", "Boeing 737-300", 1}, {"B734", "Boeing 737-400", 1}, {"B735", "Boeing 737-500", 2},
  {"B736", "Boeing 737-600", 2}, {"B737", "Boeing 737-700", 0}, {"B738", "Boeing 737-800", 0}, {"B739", "Boeing 737-900", 1},
  {"B37M", "Boeing 737 MAX 7", 2}, {"B38M", "Boeing 737 MAX 8", 0}, {"B39M", "Boeing 737 MAX 9", 1}, {"B3XM", "Boeing 737 MAX 10", 3},
  {"B752", "Boeing 757-200", 1}, {"B753", "Boeing 757-300", 2},
  {"B762", "Boeing 767-200", 2}, {"B763", "Boeing 767-300", 1}, {"B764", "Boeing 767-400", 2},
  {"B772", "Boeing 777-200", 1}, {"B77L", "Boeing 777-200LR / 777F", 1}, {"B77W", "Boeing 777-300ER", 1}, {"B779", "Boeing 777-9", 3},
  {"B788", "Boeing 787-8", 1}, {"B789", "Boeing 787-9", 1}, {"B78X", "Boeing 787-10", 1},
  {"B744", "Boeing 747-400", 3}, {"B748", "Boeing 747-8", 3}, {"B74S", "Boeing 747SP", 4}, {"BLCF", "Boeing Dreamlifter", 4},
  {"MD11", "McDonnell Douglas MD-11", 3},
  // Regional
  {"E135", "Embraer ERJ-135", 2}, {"E145", "Embraer ERJ-145", 1}, {"E170", "Embraer E170", 1}, {"E75L", "Embraer E175", 1},
  {"E190", "Embraer E190", 0}, {"E195", "Embraer E195", 0}, {"E290", "Embraer E190-E2", 1}, {"E295", "Embraer E195-E2", 1},
  {"CRJ2", "Bombardier CRJ200", 1}, {"CRJ7", "Bombardier CRJ700", 1}, {"CRJ9", "Bombardier CRJ900", 0}, {"CRJX", "Bombardier CRJ1000", 1},
  {"AT43", "ATR 42-300", 2}, {"AT45", "ATR 42-500", 1}, {"AT75", "ATR 72-500", 0}, {"AT76", "ATR 72-600", 0},
  {"DH8A", "Dash 8-100", 2}, {"DH8C", "Dash 8-300", 2}, {"DH8D", "Dash 8 Q400", 0}, {"DHC6", "Twin Otter", 2},
  {"F70", "Fokker 70", 2}, {"F100", "Fokker 100", 2}, {"RJ85", "Avro RJ85", 2}, {"RJ1H", "Avro RJ100", 2},
  {"SB20", "Saab 2000", 2}, {"SF34", "Saab 340", 2}, {"D328", "Dornier 328", 2}, {"J328", "Dornier 328JET", 2},
  {"SU95", "Suchoi Superjet 100", 3}, {"C919", "COMAC C919", 4},
  // Fracht & Exoten
  {"A124", "Antonow An-124", 4}, {"AN12", "Antonow An-12", 3}, {"IL76", "Iljuschin Il-76", 3},
  // Geschäftsreise
  {"C510", "Cessna Citation Mustang", 1}, {"C525", "Cessna CitationJet", 1}, {"C25A", "Cessna Citation CJ2", 1},
  {"C25B", "Cessna Citation CJ3", 1}, {"C25C", "Cessna Citation CJ4", 1}, {"C56X", "Cessna Citation Excel", 1},
  {"C68A", "Cessna Citation Latitude", 1}, {"C680", "Cessna Citation Sovereign", 1}, {"C700", "Cessna Citation Longitude", 2},
  {"C750", "Cessna Citation X", 2}, {"E50P", "Embraer Phenom 100", 1}, {"E55P", "Embraer Phenom 300", 1},
  {"E545", "Embraer Praetor 500", 2}, {"E550", "Embraer Praetor 600", 2}, {"E35L", "Embraer Legacy 600", 2},
  {"CL30", "Challenger 300", 1}, {"CL35", "Challenger 350", 1}, {"CL60", "Challenger 600", 1},
  {"GLEX", "Bombardier Global Express", 2}, {"GL5T", "Bombardier Global 5000", 2}, {"GL7T", "Bombardier Global 7500", 2},
  {"LJ45", "Learjet 45", 1}, {"LJ75", "Learjet 75", 1},
  {"F2TH", "Dassault Falcon 2000", 1}, {"F900", "Dassault Falcon 900", 1}, {"FA7X", "Dassault Falcon 7X", 2}, {"FA8X", "Dassault Falcon 8X", 2},
  {"G280", "Gulfstream G280", 2}, {"GLF4", "Gulfstream IV", 2}, {"GLF5", "Gulfstream V", 2}, {"GLF6", "Gulfstream G650", 2},
  {"PC12", "Pilatus PC-12", 1}, {"PC24", "Pilatus PC-24", 1}, {"HDJT", "HondaJet", 2}, {"P180", "Piaggio Avanti", 2},
  {"BE20", "Beechcraft King Air 200", 1}, {"B350", "Beechcraft King Air 350", 1}, {"BE9L", "Beechcraft King Air 90", 1},
  {"TBM7", "Daher TBM 700", 1}, {"TBM9", "Daher TBM 900", 1},
  // Kleinflugzeuge
  {"C152", "Cessna 152", 0}, {"C172", "Cessna 172", 0}, {"C182", "Cessna 182", 0}, {"C206", "Cessna 206", 1}, {"C208", "Cessna Caravan", 1},
  {"P28A", "Piper PA-28", 0}, {"PA34", "Piper Seneca", 1}, {"PA46", "Piper Malibu", 1},
  {"SR20", "Cirrus SR20", 1}, {"SR22", "Cirrus SR22", 0}, {"DA40", "Diamond DA40", 0}, {"DA42", "Diamond DA42", 1}, {"DA62", "Diamond DA62", 1},
  {"DR40", "Robin DR400", 1}, {"M20P", "Mooney M20", 1}, {"BE36", "Beechcraft Bonanza", 1}, {"E300", "Extra 300", 2},
  {"GLID", "Segelflugzeug", 1}, {"BALL", "Ballon", 2},
  // Hubschrauber
  {"EC20", "Airbus H120", 1}, {"EC30", "Airbus H130", 1}, {"EC35", "Airbus H135", 0}, {"EC45", "Airbus H145", 1},
  {"EC55", "Airbus H155", 2}, {"H160", "Airbus H160", 2}, {"EC75", "Airbus H175", 3}, {"AS50", "Airbus Ecureuil", 1},
  {"AS65", "Airbus Dauphin", 2}, {"A109", "Leonardo AW109", 1}, {"A139", "Leonardo AW139", 1}, {"A169", "Leonardo AW169", 2},
  {"R22", "Robinson R22", 1}, {"R44", "Robinson R44", 1}, {"B06", "Bell 206", 1}, {"B407", "Bell 407", 2}, {"S76", "Sikorsky S-76", 2},
  {"NH90", "NHIndustries NH90", 2}, {"H47", "Boeing CH-47 Chinook", 3}, {"UH60", "Sikorsky Black Hawk", 3}, {"H64", "Boeing AH-64 Apache", 3},
  // Militär
  {"C130", "Lockheed C-130 Hercules", 2}, {"C30J", "Lockheed C-130J", 2}, {"C17", "Boeing C-17 Globemaster", 3},
  {"K35R", "Boeing KC-135", 3}, {"E3TF", "Boeing E-3 AWACS", 3}, {"P8", "Boeing P-8 Poseidon", 3},
  {"EUFI", "Eurofighter Typhoon", 3}, {"TOR", "Panavia Tornado", 3}, {"F35", "Lockheed F-35", 3}, {"F16", "Lockheed F-16", 3},
  {"B52", "Boeing B-52", 4},
};
constexpr int NTYPES = sizeof(TYPES) / sizeof(TYPES[0]);
inline const char *RAR_NAME[5] = {"GEWÖHNLICH", "UNGEWÖHNLICH", "SELTEN", "EPISCH", "LEGENDÄR"};

struct AirlineDef { const char *code; const char *name; };
inline const AirlineDef AIRLINES[] = {
  {"DLH", "Lufthansa"}, {"CLH", "Lufthansa CityLine"}, {"DLA", "Air Dolomiti"}, {"GEC", "Lufthansa Cargo"}, {"EWG", "Eurowings"},
  {"OCN", "Discover Airlines"}, {"CFG", "Condor"}, {"TUI", "TUIfly"}, {"SXS", "SunExpress"}, {"AHO", "Air Hamburg"},
  {"BOX", "AeroLogic"}, {"BCS", "DHL (EAT Leipzig)"}, {"DHK", "DHL Air"}, {"GAF", "Luftwaffe"}, {"CHX", "ADAC Luftrettung"},
  {"RYR", "Ryanair"}, {"RUK", "Ryanair UK"}, {"LDM", "Lauda Europe"}, {"EZY", "easyJet"}, {"EJU", "easyJet Europe"},
  {"WZZ", "Wizz Air"}, {"VLG", "Vueling"}, {"TRA", "Transavia"}, {"TVF", "Transavia France"}, {"KLM", "KLM"},
  {"KLC", "KLM Cityhopper"}, {"MPH", "Martinair"}, {"AFR", "Air France"}, {"BAW", "British Airways"}, {"SHT", "BA CityFlyer"},
  {"IBE", "Iberia"}, {"TAP", "TAP Air Portugal"}, {"ITY", "ITA Airways"}, {"SWR", "Swiss"}, {"EDW", "Edelweiss"},
  {"AUA", "Austrian"}, {"BEL", "Brussels Airlines"}, {"LGL", "Luxair"}, {"SAS", "SAS"}, {"NOZ", "Norwegian"},
  {"FIN", "Finnair"}, {"ICE", "Icelandair"}, {"LOT", "LOT"}, {"CSA", "Czech Airlines"}, {"BTI", "airBaltic"},
  {"AEE", "Aegean"}, {"ROT", "TAROM"}, {"ASL", "Air Serbia"}, {"CTN", "Croatia Airlines"}, {"THY", "Turkish Airlines"},
  {"PGT", "Pegasus"}, {"EXS", "Jet2"}, {"TOM", "TUI Airways"}, {"OAW", "Helvetic"}, {"UAE", "Emirates"},
  {"QTR", "Qatar Airways"}, {"ETD", "Etihad"}, {"SVA", "Saudia"}, {"ELY", "El Al"}, {"MSR", "EgyptAir"},
  {"RAM", "Royal Air Maroc"}, {"ETH", "Ethiopian"}, {"DAL", "Delta"}, {"UAL", "United"}, {"AAL", "American"},
  {"ACA", "Air Canada"}, {"SIA", "Singapore Airlines"}, {"CPA", "Cathay Pacific"}, {"CCA", "Air China"}, {"CES", "China Eastern"},
  {"CSN", "China Southern"}, {"ANA", "ANA"}, {"JAL", "Japan Airlines"}, {"KAL", "Korean Air"}, {"AIC", "Air India"},
  {"KZR", "Air Astana"}, {"UPS", "UPS"}, {"FDX", "FedEx"}, {"CLX", "Cargolux"}, {"GTI", "Atlas Air"},
  {"ABR", "ASL Airlines"}, {"CAO", "Air China Cargo"}, {"NJE", "NetJets"}, {"VJT", "VistaJet"}, {"RCH", "US Air Force"},
  {"NATO", "NATO"}, {"BPO", "Bundespolizei"},
};
constexpr int NAIRLINES = sizeof(AIRLINES) / sizeof(AIRLINES[0]);

inline int catIndex(const char *code) { for (int i = 0; i < NTYPES; i++) if (!strcmp(TYPES[i].code, code)) return i; return -1; }
inline const char *airlineName(const char *code) { for (auto &a : AIRLINES) if (!strcmp(a.code, code)) return a.name; return nullptr; }

// ---- Sammlung ----------------------------------------------------------------------------
struct TRec { char code[5]; uint8_t pad[3]; uint32_t first, last, count; char call[9]; char reg[9]; uint8_t pad2[2]; };
struct LRec { char code[5]; uint8_t pad[3]; uint32_t first, last, count; char call[9]; uint8_t pad2[3]; };
struct PRec { char code[5]; uint8_t pad[3]; uint32_t first, last, count; };
constexpr int MAXT = 300, MAXL = 200, MAXP = 300;

inline TRec types[MAXT]; inline int nTypes = 0;
inline LRec lines[MAXL]; inline int nLines = 0;
inline PRec ports[MAXP]; inline int nPorts = 0;
inline bool dirty = false, ready = false;
inline uint32_t lastSave = 0;
inline SemaphoreHandle_t lock = nullptr;

// neue Karten für die Anzeige (Index in TYPES oder -1 = unbekannter Typ)
struct NewCard { char code[5]; int8_t rar; };
inline NewCard queue[8]; inline int qHead = 0, qLen = 0;

template <class T> inline int loadFile(const char *path, T *arr, int max) {
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  int n = f.read((uint8_t *)arr, sizeof(T) * max) / sizeof(T);
  f.close();
  return n;
}
template <class T> inline void saveFile(const char *path, const T *arr, int n) {
  File f = LittleFS.open(path, "w");
  if (!f) return;
  f.write((const uint8_t *)arr, sizeof(T) * n);
  f.close();
}

inline void begin() {
  lock = xSemaphoreCreateMutex();
  if (!LittleFS.begin(true)) { Serial.println("Album: kein Speicher"); return; }
  LittleFS.remove("/frames.bin");                     // gab es nur in 0.7.7
  nTypes = loadFile("/types.bin", types, MAXT);
  nLines = loadFile("/airlines.bin", lines, MAXL);
  nPorts = loadFile("/airports.bin", ports, MAXP);
  ready = true;
  Serial.printf("Album: %d Typen, %d Airlines, %d Flughäfen\n", nTypes, nLines, nPorts);
}

inline uint32_t nowTs() { return Net::timeValid() ? (uint32_t)time(nullptr) : 0; }

inline void pushCard(const char *code) {
  int ci = catIndex(code);
  int8_t rar = ci >= 0 ? TYPES[ci].rar : -1;
  if (qLen == 8) {                                    // voll: gewöhnliche zuerst verwerfen
    if (rar <= 0) return;
    qHead = (qHead + 1) % 8; qLen--;
  }
  NewCard &c = queue[(qHead + qLen) % 8]; strncpy(c.code, code, 4); c.code[4] = 0; c.rar = rar; qLen++;
}

// Airline aus dem Rufzeichen: drei Buchstaben, dann eine Ziffer (DLH4CK → DLH)
inline bool airlineOf(const char *call, char out[5]) {
  if (strlen(call) < 4) return false;
  for (int i = 0; i < 3; i++) if (!isalpha((unsigned char)call[i])) return false;
  if (!strncmp(call, "NATO", 4)) { strcpy(out, "NATO"); return true; }
  if (!isdigit((unsigned char)call[3])) return false;
  memcpy(out, call, 3); out[3] = 0;
  return true;
}

// Ein Flugzeug zum ersten Mal heute im Radius (aus Stats::count, Hintergrund-Task)
inline void sight(const FlightParse::Plane &p) {
  if (!ready) return;
  uint32_t now = nowTs();
  if (!now) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  // Typ
  if (p.type[0]) {
    int i = 0;
    while (i < nTypes && strcmp(types[i].code, p.type)) i++;
    if (i == nTypes && nTypes < MAXT) {
      TRec &t = types[nTypes++]; memset(&t, 0, sizeof(t));
      strncpy(t.code, p.type, 4); t.first = now; strncpy(t.call, p.call, 8); strncpy(t.reg, p.reg, 8);
      pushCard(p.type);
      int ci = catIndex(p.type);
      if (ci >= 0 && TYPES[ci].rar == 4) Badges::earn(Badges::LEGEND);
    }
    if (i < nTypes) { types[i].last = now; types[i].count++; }
    int have = 0; for (int k = 0; k < nTypes; k++) if (catIndex(types[k].code) >= 0) have++;
    if (have >= 10) Badges::earn(Badges::TYPES10);
    if (have >= 50) Badges::earn(Badges::TYPES50);
  }
  // Airline
  char al[5];
  if (airlineOf(p.call, al)) {
    int i = 0;
    while (i < nLines && strcmp(lines[i].code, al)) i++;
    if (i == nLines && nLines < MAXL) { LRec &l = lines[nLines++]; memset(&l, 0, sizeof(l)); strcpy(l.code, al); l.first = now; strncpy(l.call, p.call, 8); }
    if (i < nLines) { lines[i].last = now; lines[i].count++; }
  }
  dirty = true;
  xSemaphoreGive(lock);
}

// Flughafen aus einer gefundenen Route
inline void airport(const char *code) {
  if (!ready || !code[0]) return;
  uint32_t now = nowTs();
  if (!now) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  int i = 0;
  while (i < nPorts && strcmp(ports[i].code, code)) i++;
  if (i == nPorts && nPorts < MAXP) { PRec &r = ports[nPorts++]; memset(&r, 0, sizeof(r)); strncpy(r.code, code, 4); r.first = now; }
  if (i < nPorts) { ports[i].last = now; ports[i].count++; }
  dirty = true;
  xSemaphoreGive(lock);
}

// Speichern: gesammelt alle 10 Minuten (schont den Flash)
inline void flush(bool force = false) {
  if (!ready || !dirty) return;
  if (!force && millis() - lastSave < 10UL * 60000UL) return;
  xSemaphoreTake(lock, portMAX_DELAY);
  saveFile("/types.bin", types, nTypes);
  saveFile("/airlines.bin", lines, nLines);
  saveFile("/airports.bin", ports, nPorts);
  dirty = false; lastSave = millis();
  xSemaphoreGive(lock);
}

// Für die Anzeige: nächste neue Karte als Text, z. B. „NEUE KARTE: AIRBUS A380 · EPISCH!“
inline bool takeCard(String &text, int &rar) {
  if (!lock) return false;
  xSemaphoreTake(lock, portMAX_DELAY);
  bool ok = qLen > 0;
  if (ok) {
    NewCard c = queue[qHead]; qHead = (qHead + 1) % 8; qLen--;
    int ci = catIndex(c.code);
    rar = c.rar;
    String name = ci >= 0 ? TYPES[ci].name : c.code;
    name.toUpperCase();
    text = "NEUE KARTE: " + name;
    if (ci >= 0) text += String("  ") + RAR_NAME[TYPES[ci].rar] + "!";
    if (ci >= 0) { int have = 0; for (int k = 0; k < nTypes; k++) if (catIndex(types[k].code) >= 0) have++; text += "  " + String(have) + "/" + String(NTYPES); }
  }
  xSemaphoreGive(lock);
  return ok;
}

inline int typesHave() {
  int have = 0;
  xSemaphoreTake(lock, portMAX_DELAY);
  for (int k = 0; k < nTypes; k++) if (catIndex(types[k].code) >= 0) have++;
  xSemaphoreGive(lock);
  return have;
}

// ---- JSON fürs Webinterface ----------------------------------------------------------------
inline String esc(const char *s) { String r = s; r.replace("\\", "/"); r.replace("\"", "'"); return r; }

inline String jsonTypes() {
  String j; j.reserve(24000);
  xSemaphoreTake(lock, portMAX_DELAY);
  int have = 0;
  j = "{\"cards\":[";
  for (int i = 0; i < NTYPES; i++) {
    const TypeDef &d = TYPES[i];
    int k = 0; while (k < nTypes && strcmp(types[k].code, d.code)) k++;
    if (i) j += ",";
    j += "{\"n\":" + String(i + 1) + ",\"code\":\"" + d.code + "\",\"name\":\"" + d.name + "\",\"r\":" + String(d.rar);
    if (k < nTypes) {
      have++;
      const TRec &t = types[k];
      j += ",\"got\":true,\"first\":" + String(t.first) + ",\"last\":" + String(t.last) + ",\"count\":" + String(t.count) +
           ",\"call\":\"" + esc(t.call) + "\",\"reg\":\"" + esc(t.reg) + "\"";
    }
    j += "}";
  }
  j += "],\"extra\":[";
  bool first = true;
  for (int k = 0; k < nTypes; k++) {
    if (catIndex(types[k].code) >= 0) continue;
    const TRec &t = types[k];
    if (!first) j += ",";
    first = false;
    j += "{\"code\":\"" + esc(t.code) + "\",\"first\":" + String(t.first) + ",\"count\":" + String(t.count) + ",\"call\":\"" + esc(t.call) + "\",\"reg\":\"" + esc(t.reg) + "\"}";
  }
  j += "],\"have\":" + String(have) + ",\"total\":" + String(NTYPES) + "}";
  xSemaphoreGive(lock);
  return j;
}

inline String jsonAirlines() {
  String j; j.reserve(12000);
  xSemaphoreTake(lock, portMAX_DELAY);
  int have = 0;
  j = "{\"cards\":[";
  for (int i = 0; i < NAIRLINES; i++) {
    int k = 0; while (k < nLines && strcmp(lines[k].code, AIRLINES[i].code)) k++;
    if (i) j += ",";
    j += "{\"n\":" + String(i + 1) + ",\"code\":\"" + AIRLINES[i].code + "\",\"name\":\"" + AIRLINES[i].name + "\"";
    if (k < nLines) { have++; j += ",\"got\":true,\"first\":" + String(lines[k].first) + ",\"count\":" + String(lines[k].count) + ",\"call\":\"" + esc(lines[k].call) + "\""; }
    j += "}";
  }
  j += "],\"extra\":[";
  bool first = true;
  for (int k = 0; k < nLines; k++) {
    if (airlineName(lines[k].code)) continue;
    if (!first) j += ",";
    first = false;
    j += "{\"code\":\"" + esc(lines[k].code) + "\",\"first\":" + String(lines[k].first) + ",\"count\":" + String(lines[k].count) + ",\"call\":\"" + esc(lines[k].call) + "\"}";
  }
  j += "],\"have\":" + String(have) + ",\"total\":" + String(NAIRLINES) + "}";
  xSemaphoreGive(lock);
  return j;
}

inline String jsonAirports() {
  String j; j.reserve(8000);
  xSemaphoreTake(lock, portMAX_DELAY);
  j = "{\"list\":[";
  for (int k = 0; k < nPorts; k++) {
    if (k) j += ",";
    j += "{\"code\":\"" + esc(ports[k].code) + "\",\"first\":" + String(ports[k].first) + ",\"last\":" + String(ports[k].last) + ",\"count\":" + String(ports[k].count) + "}";
  }
  j += "]}";
  xSemaphoreGive(lock);
  return j;
}

inline void reset() {
  xSemaphoreTake(lock, portMAX_DELAY);
  nTypes = nLines = nPorts = 0; qLen = 0;
  LittleFS.remove("/types.bin"); LittleFS.remove("/airlines.bin"); LittleFS.remove("/airports.bin");
  dirty = false;
  xSemaphoreGive(lock);
}

}  // namespace Album
