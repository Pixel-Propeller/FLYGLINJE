#pragma once
// ---------------------------------------------------------------------------
// Antwort-Körper einer HTTP/1.1-Antwort entpacken – ohne Arduino-Abhängigkeiten
// (auch am PC testbar):
//   "chunked"  -> Stückelung entfernen
//   "gzip"     -> mit dem Inflate aus dem ESP32-ROM entpacken (kostet keinen
//                 Flash-Platz, braucht während der Abfrage ca. 43 KB RAM)
// Heraus kommen die reinen Nutzdaten, Byte für Byte an eine Rückruffunktion.
// ---------------------------------------------------------------------------
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include "miniz.h"

namespace HttpBody {

typedef void (*Sink)(const uint8_t *data, size_t len, void *user);

struct Decoder {
  // --- Einstellungen ---
  bool chunked = false, gzip = false;
  Sink sink = nullptr; void *user = nullptr;

  // --- Zustand Stückelung ---
  enum CState : uint8_t { C_SIZE, C_EXT, C_SIZE_LF, C_DATA, C_DATA_CR, C_DATA_LF, C_TRAILER, C_TRAILER_LF, C_DONE };
  CState cs = C_SIZE;
  uint32_t chunkLeft = 0;
  bool sizeDigits = false;
  bool trailerEmpty = true;

  // --- Zustand gzip ---
  enum GState : uint8_t { G_HEAD, G_EXTRA_LEN, G_EXTRA, G_NAME, G_COMMENT, G_HCRC, G_BODY, G_DONE };
  GState gs = G_HEAD;
  uint8_t head[10]; uint8_t headLen = 0, flags = 0;
  uint16_t skip = 0; uint8_t skipLen = 0;
  tinfl_decompressor *inf = nullptr;
  uint8_t *dict = nullptr; size_t dictOfs = 0;
  uint8_t in[256]; size_t inLen = 0;

  bool failed = false;
  const char *error = nullptr;
  size_t produced = 0;      // Nutzdaten insgesamt
  size_t maxOut = 0;        // 0 = unbegrenzt (Schutz gegen "Zip-Bomben")

  bool begin(bool isChunked, bool isGzip, Sink s, void *u, size_t limit = 0) {
    end();
    chunked = isChunked; gzip = isGzip; sink = s; user = u; maxOut = limit;
    cs = C_SIZE; chunkLeft = 0; sizeDigits = false; trailerEmpty = true;
    gs = G_HEAD; headLen = 0; flags = 0; skip = 0; skipLen = 0; inLen = 0; dictOfs = 0;
    failed = false; error = nullptr; produced = 0;
    if (gzip) {
      inf = (tinfl_decompressor *)malloc(sizeof(tinfl_decompressor));
      dict = (uint8_t *)malloc(TINFL_LZ_DICT_SIZE);
      if (!inf || !dict) { fail("zu wenig Speicher zum Entpacken"); return false; }
      tinfl_init(inf);
    }
    return true;
  }

  void end() {
    free(inf); inf = nullptr;
    free(dict); dict = nullptr;
  }
  ~Decoder() { end(); }

  void fail(const char *e) { if (!failed) { failed = true; error = e; } }

  // Fertig = Ende der Stückelung bzw. Ende des gzip-Datenstroms erreicht
  bool finished() const {
    if (chunked) return cs == C_DONE;   // erst nach dem letzten Stück – sonst bleiben Reste in der Leitung
    if (gzip) return gs == G_DONE;
    return false;    // ohne beides entscheidet Content-Length / Verbindungsende
  }

  // Rohdaten von der Leitung hineingeben. false = Fehler
  bool feed(const uint8_t *p, size_t n) {
    if (failed) return false;
    if (!chunked) { payload(p, n); return !failed; }
    size_t i = 0;
    while (i < n && !failed) {
      uint8_t c = p[i];
      switch (cs) {
        case C_SIZE: {
          int v = hexVal(c);
          if (v >= 0) {
            if (chunkLeft > 0x0FFFFFFF) { fail("Stück zu groß"); break; }
            chunkLeft = chunkLeft * 16 + v; sizeDigits = true; i++;
          } else if (c == ';' || c == ' ' || c == '\t') { cs = C_EXT; i++; }
          else if (c == '\r') { cs = C_SIZE_LF; i++; }
          else if (c == '\n') { i++; afterSize(); }
          else fail("Stückelung fehlerhaft");
          break;
        }
        case C_EXT: if (c == '\r') cs = C_SIZE_LF; else if (c == '\n') afterSize(); i++; break;
        case C_SIZE_LF: if (c != '\n') { fail("Stückelung fehlerhaft"); break; } i++; afterSize(); break;
        case C_DATA: {
          size_t take = n - i; if (take > chunkLeft) take = chunkLeft;
          payload(p + i, take);
          i += take; chunkLeft -= take;
          if (chunkLeft == 0) cs = C_DATA_CR;
          break;
        }
        case C_DATA_CR: if (c == '\r') { cs = C_DATA_LF; i++; } else if (c == '\n') { cs = C_SIZE; i++; } else fail("Stückelung fehlerhaft"); break;
        case C_DATA_LF: if (c != '\n') { fail("Stückelung fehlerhaft"); break; } cs = C_SIZE; i++; break;
        case C_TRAILER:
          if (c == '\r') { i++; break; }
          if (c == '\n') { i++; if (trailerEmpty) cs = C_DONE; trailerEmpty = true; break; }
          trailerEmpty = false; i++; break;
        default: i = n; break;   // C_DONE: Rest ignorieren
      }
    }
    return !failed;
  }

private:
  static int hexVal(uint8_t c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
  void afterSize() {
    if (!sizeDigits) { fail("Stückelung fehlerhaft"); return; }
    sizeDigits = false;
    if (chunkLeft == 0) { cs = C_TRAILER; trailerEmpty = true; }
    else cs = C_DATA;
  }

  void emit(const uint8_t *p, size_t n) {
    if (!n) return;
    produced += n;
    if (maxOut && produced > maxOut) { fail("Antwort zu groß"); return; }
    if (sink) sink(p, n, user);
  }

  // Nutzdaten (nach der Stückelung): direkt weiter oder durch gzip
  void payload(const uint8_t *p, size_t n) {
    if (!gzip) { emit(p, n); return; }
    size_t i = 0;
    while (i < n && !failed && gs != G_DONE) {
      uint8_t c = p[i];
      switch (gs) {
        case G_HEAD:
          head[headLen++] = c; i++;
          if (headLen == 10) {
            if (head[0] != 0x1f || head[1] != 0x8b || head[2] != 8) { fail("kein gzip"); break; }
            flags = head[3];
            nextHeaderPart();
          }
          break;
        case G_EXTRA_LEN:
          skip |= (uint16_t)c << (8 * skipLen); i++;
          if (++skipLen == 2) { gs = G_EXTRA; if (skip == 0) { flags &= ~4; nextHeaderPart(); } }
          break;
        case G_EXTRA: i++; if (--skip == 0) { flags &= ~4; nextHeaderPart(); } break;
        case G_NAME: i++; if (c == 0) { flags &= ~8; nextHeaderPart(); } break;
        case G_COMMENT: i++; if (c == 0) { flags &= ~16; nextHeaderPart(); } break;
        case G_HCRC: i++; if (++skipLen == 2) { flags &= ~2; nextHeaderPart(); } break;
        case G_BODY: {
          size_t take = sizeof(in) - inLen; if (take > n - i) take = n - i;
          memcpy(in + inLen, p + i, take); inLen += take; i += take;
          inflateSome(false);
          break;
        }
        default: i = n; break;
      }
    }
  }

  void nextHeaderPart() {
    if (flags & 4) { gs = G_EXTRA_LEN; skip = 0; skipLen = 0; }
    else if (flags & 8) gs = G_NAME;
    else if (flags & 16) gs = G_COMMENT;
    else if (flags & 2) { gs = G_HCRC; skipLen = 0; }
    else gs = G_BODY;
  }

  void inflateSome(bool last) {
    while (!failed) {
      size_t inBytes = inLen;
      size_t outBytes = TINFL_LZ_DICT_SIZE - dictOfs;
      tinfl_status st = tinfl_decompress(inf, in, &inBytes, dict, dict + dictOfs, &outBytes,
                                         last ? 0 : TINFL_FLAG_HAS_MORE_INPUT);
      if (inBytes) { memmove(in, in + inBytes, inLen - inBytes); inLen -= inBytes; }
      emit(dict + dictOfs, outBytes);
      dictOfs = (dictOfs + outBytes) & (TINFL_LZ_DICT_SIZE - 1);
      if (st == TINFL_STATUS_DONE) { gs = G_DONE; return; }
      if (st < 0) { fail("gzip beschädigt"); return; }
      if (st == TINFL_STATUS_NEEDS_MORE_INPUT && inLen == 0) return;
      if (st == TINFL_STATUS_NEEDS_MORE_INPUT && inBytes == 0 && outBytes == 0) return;  // braucht mehr als im Puffer
      if (st == TINFL_STATUS_HAS_MORE_OUTPUT) continue;
      if (inBytes == 0 && outBytes == 0) return;
    }
  }
};

}  // namespace HttpBody
