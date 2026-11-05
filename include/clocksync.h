#pragma once
#include <Arduino.h>
#include <time.h>

// ============================================================
//  clocksync.h - NTP time with timezone support
// ============================================================

void     clockBegin(const char* tz);
void     clockOnWifi();                // call whenever WiFi (re)connects: restarts SNTP
const char* clockServerInUse();
// time as text, honouring the 12/24-hour setting.  parts: hm = "9:05" or "21:05", ampm = "PM" (empty in 24-hour mode)
void     clockParts(const struct tm& t, char* hm, size_t hmLen, char* ampm, size_t apLen);
String   clockFormat(const struct tm& t);        // "9:05 PM" or "21:05"
void     clockSetTz(const char* tz);   // re-sync with new TZ
bool     clockOk();                    // time is valid (year > 2020)
bool     clockSynced();                // NTP synced at least once
void     clockTick();                  // periodic resync
time_t   clockNow();
String   clockTimeStr();               // "14:03"
String   clockTimeStrSec();            // "14:03:27"
String   clockDateStr();               // "27 Oct 2026"
String   clockStamp();                 // "2026-10-27T14:03:27"
const char* clockTz();
