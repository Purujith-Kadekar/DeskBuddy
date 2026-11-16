#pragma once
#include <Arduino.h>

// ============================================================
//  jsonutil.h - the tiny JSON scanner + text helpers shared by
//  webapi.cpp (device REST API) and cloudsync.cpp (Vercel sync).
//  No ArduinoJson dependency - same style as the weather client.
// ============================================================

// Finds the VALUE position of a key. The scanner walks the text token by token and skips whole
// string values, so a label such as  say "time" now  can never be mistaken for the key "time".
static inline bool jFind(const String& b, const char* key, int& pos) {
  const int n = (int)b.length();
  const size_t kl = strlen(key);
  int i = 0;
  while (i < n) {
    if (b[i] != '"') { i++; continue; }
    i++;                                             // inside a string token
    size_t k = 0;
    bool match = true;
    while (i < n && b[i] != '"') {
      if (b[i] == '\\') { match = false; i += 2; continue; }
      if (match) { if (k < kl && b[i] == key[k]) k++; else match = false; }
      i++;
    }
    if (i >= n) return false;
    i++;                                             // closing quote
    if (match && k == kl) {
      int j = i;
      while (j < n && (b[j] == ' ' || b[j] == '\t' || b[j] == '\n' || b[j] == '\r')) j++;
      if (j < n && b[j] == ':') {                    // a key is a string followed by ':'
        j++;
        while (j < n && (b[j] == ' ' || b[j] == '\t' || b[j] == '\n' || b[j] == '\r')) j++;
        pos = j;
        return true;
      }
    }
  }
  return false;
}

static inline bool jInt(const String& b, const char* key, long& out) {
  int i;
  if (!jFind(b, key, i)) return false;
  if (!(b[i] == '-' || (b[i] >= '0' && b[i] <= '9'))) return false;   // "null"/true/etc. is not a number
  out = strtol(b.c_str() + i, nullptr, 10);
  return true;
}

static inline bool jStr(const String& b, const char* key, String& out) {
  int i;
  if (!jFind(b, key, i)) return false;
  if (b[i] != '"') return false;
  i++;
  String s;
  while (i < (int)b.length() && b[i] != '"') {
    char c = b[i];
    if (c == '\\' && i + 1 < (int)b.length()) { i++; c = b[i]; }
    if ((uint8_t)c >= 32) s += c;
    i++;
  }
  out = s;
  return true;
}

static inline bool jBool(const String& b, const char* key, bool& out) {
  int i;
  if (!jFind(b, key, i)) return false;
  if (b[i] == 't') { out = true; return true; }
  if (b[i] == 'f') { out = false; return true; }
  out = strtol(b.c_str() + i, nullptr, 10) != 0;
  return true;
}

static inline bool jNum(const String& b, const char* key, double& out) {
  int i;
  if (!jFind(b, key, i)) return false;
  out = strtod(b.c_str() + i, nullptr);
  return true;
}

// printable-ASCII-only text for the OLED fonts (anything else would draw as garbage)
static inline void sanitizeLabel(const String& in, char* out, size_t cap) {
  size_t j = 0;
  for (size_t i = 0; i < in.length() && j < cap - 1; i++) {
    char c = in[i];
    if (c == '"' || c == '\\' || c == '<' || (uint8_t)c < 32 || (uint8_t)c > 126) continue;
    out[j++] = c;
  }
  out[j] = 0;
}

// "YYYY-MM-DD" -> numbers (digits only; the caller validates the calendar date)
static inline bool parseDateStr(const String& d, uint16_t& y, uint8_t& mo, uint8_t& da) {
  if (d.length() < 10 || d[4] != '-' || d[7] != '-') return false;
  for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
    if (d[i] < '0' || d[i] > '9') return false;
  y = (uint16_t)d.substring(0, 4).toInt();
  mo = (uint8_t)d.substring(5, 7).toInt();
  da = (uint8_t)d.substring(8, 10).toInt();
  return true;
}

// "HH:MM" -> numbers (false when malformed)
static inline bool parseHMStr(const String& s, uint8_t& h, uint8_t& m) {
  int c = s.indexOf(':');
  if (c <= 0) return false;
  long hh = s.substring(0, c).toInt(), mm = s.substring(c + 1).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;
  h = (uint8_t)hh; m = (uint8_t)mm;
  return true;
}

// a fresh local id ("L" + 11 hex chars; see alarms.h for the id scheme)
static inline void makeId(char out[13]) {
  snprintf(out, 13, "L%011llx",
           (unsigned long long)(((uint64_t)random(0x10000) << 32) |
                                ((uint32_t)random(0x10000) << 16) |
                                (uint32_t)(random(0x10000) & 0xFFFFULL)));
}
