#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"

// ============================================================
//  alarms.h - persistent alarm engine (NVS)
//  kinds: ONCE (any future date, even next year), DAILY,
//         WEEKLY (weekday bitmask), COUNTDOWN (absolute after)
// ============================================================

enum AlarmKind : uint8_t { AK_ONCE = 0, AK_DAILY, AK_WEEKLY, AK_COUNTDOWN };

// an entry is either a classic ALARM (rings with the full-screen alarm) or a
// REMINDER (slides in like a phone notification, then rings until tapped)
enum AlarmType : uint8_t { AT_ALARM = 0, AT_REMINDER = 1 };

#define ALARM_LABEL_LEN 32      // 31 visible characters

struct __attribute__((packed)) Alarm {
  uint8_t  enabled;    // 1 = active
  uint8_t  kind;       // AlarmKind
  uint8_t  hour;       // 0..23
  uint8_t  minute;     // 0..59
  uint8_t  weekdays;   // bit0=Mon .. bit6=Sun (AK_WEEKLY only)
  uint16_t year;       // AK_ONCE / AK_COUNTDOWN target
  uint8_t  month;      // 1..12
  uint8_t  day;        // 1..31
  uint8_t  melody;     // MelodyId
  uint8_t  type;       // AlarmType
  char     label[ALARM_LABEL_LEN];  // zero-terminated, sanitized
};

void    alarmsBegin();                    // load from NVS
int     alarmsCount();
int     alarmsCapacity();
Alarm   alarmGet(int i);                  // returns zeroed Alarm if bad idx
int     alarmAdd(const Alarm& a);          // idx or -1 (full)
bool    alarmUpdate(int i, const Alarm& a);
bool    alarmRemove(int i);
void    alarmsSave();                      // persist now (auto after mutations)

// called once per minute; returns the first alarm idx >= from that is due now, else -1
// (call again with from = idx + 1 to collect several alarms that share a minute)
int     alarmsCheck(const struct tm& now, int from = 0);

// one-time / countdown entries only: epoch of their target moment (false for daily / weekly)
bool    alarmTargetTime(const Alarm& a, time_t& out);

// true when y-m-d is a real calendar date (Feb 30 and friends are not)
bool    validDate(int y, int m, int d);

// next occurrence of an alarm (epoch) + human "in 2d 4h"
bool    alarmNextFire(int i, time_t& outEpoch);
String  alarmNextIn(int i);                // "in 2d 4h 15m" / "expired"

// snooze slot (RAM only, not persisted)
void    alarmSnooze(int minutes, const char* label, uint8_t melody, uint8_t type = AT_ALARM);
bool    snoozeActive();
bool    snoozeFired();      // true once when time reached (consume-ish)
const char* snoozeLabel();
uint8_t snoozeMelody();
uint8_t snoozeType();
void    snoozeClear();
time_t  snoozeRemaining(); // seconds until snooze fires (0 if none)
