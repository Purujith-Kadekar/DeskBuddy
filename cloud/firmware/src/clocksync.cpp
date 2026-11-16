#include "clocksync.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>

static bool     synced = false;
static char     tzBuf[48] = TZ_DEFAULT;
static uint32_t kickMs = 0;          // when SNTP was last (re)started
static bool     fallback = false;    // using the global pool because India's servers did not answer
static bool     started = false;

static void startSntp(bool useFallback) {
  fallback = useFallback;
  started = true;
  kickMs = millis();
  if (useFallback) configTzTime(tzBuf, NTP_SERVER_1, NTP_FALLBACK_1, NTP_FALLBACK_2);
  else             configTzTime(tzBuf, NTP_SERVER_1, NTP_SERVER_2, NTP_SERVER_3);
  Serial.printf("[TIME] SNTP started (%s) tz=%s\n", useFallback ? "fallback pool" : "NPL/NIC India", tzBuf);
}

void clockBegin(const char* tz) {
  strncpy(tzBuf, tz && tz[0] ? tz : TZ_DEFAULT, sizeof(tzBuf) - 1);
  tzBuf[sizeof(tzBuf) - 1] = 0;
  setenv("TZ", tzBuf, 1);            // local time is correct even before the first sync
  tzset();
  started = false;
}

// WiFi just (re)connected -> (re)start SNTP now that DNS + routing exist
void clockOnWifi() { startSntp(false); }

void clockSetTz(const char* tz) {
  clockBegin(tz);
  if (WiFi.status() == WL_CONNECTED) startSntp(fallback);
}

const char* clockServerInUse() { return fallback ? "pool.ntp.org" : "NPL / NIC India"; }

bool clockOk() {
  time_t t = time(nullptr);
  struct tm tmv;
  localtime_r(&t, &tmv);
  return (tmv.tm_year + 1900) >= 2025;      // anything earlier = never synced
}

bool clockSynced() {
  if (!synced && clockOk()) {
    synced = true;
    Serial.printf("[TIME] synced via %s\n", clockServerInUse());
  }
  return synced;
}

void clockTick() {
  if (WiFi.status() != WL_CONNECTED || !started) return;
  clockSynced();
  uint32_t now = millis();
  if (!clockOk()) {
    // India's servers silent for 45 s -> also try the global pool, alternate every 45 s
    if (now - kickMs > (uint32_t)NTP_FALLBACK_AFTER_S * 1000UL) startSntp(!fallback);
  } else if (now - kickMs > NTP_RESYNC_S * 1000UL) {
    startSntp(false);                        // hourly refresh from India's servers
  }
}

time_t clockNow() { return time(nullptr); }

void clockParts(const struct tm& t, char* hm, size_t hmLen, char* ampm, size_t apLen) {
  if (settings.hour12) {
    int h = t.tm_hour % 12;
    if (h == 0) h = 12;
    snprintf(hm, hmLen, "%d:%02d", h, t.tm_min);
    snprintf(ampm, apLen, "%s", t.tm_hour >= 12 ? "PM" : "AM");
  } else {
    snprintf(hm, hmLen, "%02d:%02d", t.tm_hour, t.tm_min);
    if (apLen) ampm[0] = 0;
  }
}

String clockFormat(const struct tm& t) {
  char hm[8], ap[4];
  clockParts(t, hm, sizeof(hm), ap, sizeof(ap));
  return ap[0] ? String(hm) + " " + ap : String(hm);
}

String clockTimeStr() {
  if (!clockOk()) return "--:--";
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  return clockFormat(tmv);
}

String clockTimeStrSec() {
  if (!clockOk()) return "--:--:--";
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  char b[12]; strftime(b, sizeof(b), "%H:%M:%S", &tmv);
  return String(b);
}

String clockDateStr() {
  if (!clockOk()) return "-- --- ----";
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  char b[16]; strftime(b, sizeof(b), "%d %b %Y", &tmv);
  return String(b);
}

String clockStamp() {
  if (!clockOk()) return "1970-01-01T00:00:00";
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  char b[24]; strftime(b, sizeof(b), "%Y-%m-%dT%H:%M:%S", &tmv);
  return String(b);
}

const char* clockTz() { return tzBuf; }
