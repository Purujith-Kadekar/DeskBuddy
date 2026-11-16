#include "alarms.h"
#include <Preferences.h>

static_assert(sizeof(Alarm) == 43, "Alarm layout changed - NVS migration needed");

// previous on-flash layout (v1): 26 bytes, 16-char label, no type
struct __attribute__((packed)) AlarmV1 {
  uint8_t enabled, kind, hour, minute, weekdays;
  uint16_t year;
  uint8_t month, day, melody;
  char label[16];
};
static_assert(sizeof(AlarmV1) == 26, "AlarmV1 layout");

#define AL_MAGIC 0xB2
static Alarm list[MAX_ALARMS];
static uint8_t nAlarms = 0;

// snooze slot
static bool    snzActive = false;
static time_t  snzAt = 0;
static char    snzLabel[ALARM_LABEL_LEN] = "";
static uint8_t snzMelody = 0;
static uint8_t snzType = AT_ALARM;

void alarmsSave() {
  Preferences p;
  p.begin("buddy", false);
  uint8_t buf[1 + 1 + 1 + MAX_ALARMS * sizeof(Alarm)];
  buf[0] = AL_MAGIC;
  buf[1] = 2;                 // layout version
  buf[2] = nAlarms;
  memcpy(buf + 3, list, nAlarms * sizeof(Alarm));
  p.putBytes("alarms", buf, 3 + nAlarms * sizeof(Alarm));
  p.end();
}

void alarmsBegin() {
  memset(list, 0, sizeof(list));
  nAlarms = 0;
  Preferences p;
  p.begin("buddy", true);
  size_t len = p.getBytesLength("alarms");
  if (len >= 3 && len <= 3 + MAX_ALARMS * sizeof(Alarm)) {
    static uint8_t buf[3 + MAX_ALARMS * sizeof(Alarm)];
    p.getBytes("alarms", buf, len);
    if (buf[0] == AL_MAGIC && buf[1] == 2 && buf[2] <= MAX_ALARMS &&
        len >= 3 + (size_t)buf[2] * sizeof(Alarm)) {
      nAlarms = buf[2];
      memcpy(list, buf + 3, nAlarms * sizeof(Alarm));
    } else if (buf[0] == AL_MAGIC && buf[1] == 1 && buf[2] <= MAX_ALARMS &&
               len >= 3 + (size_t)buf[2] * sizeof(AlarmV1)) {
      // migrate v1 -> v2 (all old entries were alarms)
      nAlarms = buf[2];
      for (int i = 0; i < nAlarms; i++) {
        AlarmV1 o;
        memcpy(&o, buf + 3 + i * sizeof(AlarmV1), sizeof(o));
        Alarm& n = list[i];
        n.enabled = o.enabled; n.kind = o.kind; n.hour = o.hour; n.minute = o.minute;
        n.weekdays = o.weekdays; n.year = o.year; n.month = o.month; n.day = o.day;
        n.melody = o.melody; n.type = AT_ALARM;
        memcpy(n.label, o.label, sizeof(o.label));
        n.label[sizeof(o.label)] = 0;
      }
      Serial.println("[ALARM] migrated saved alarms to the new format");
    }
  }
  p.end();
  if (nAlarms && len) alarmsSave();      // persist the migrated layout
  Serial.printf("[ALARM] loaded %d entries\n", (int)nAlarms);
}

int alarmsCount() { return nAlarms; }
int alarmsCapacity() { return MAX_ALARMS; }

Alarm alarmGet(int i) {
  Alarm a; memset(&a, 0, sizeof(a));
  if (i >= 0 && i < nAlarms) a = list[i];
  return a;
}

int alarmAdd(const Alarm& a) {
  if (nAlarms >= MAX_ALARMS) return -1;
  list[nAlarms++] = a;
  alarmsSave();
  return nAlarms - 1;
}

bool alarmUpdate(int i, const Alarm& a) {
  if (i < 0 || i >= nAlarms) return false;
  list[i] = a;
  alarmsSave();
  return true;
}

bool alarmRemove(int i) {
  if (i < 0 || i >= nAlarms) return false;
  for (int j = i; j < nAlarms - 1; j++) list[j] = list[j + 1];
  nAlarms--;
  alarmsSave();
  return true;
}

bool validDate(int y, int m, int d) {
  if (y < 2021 || y > 2099 || m < 1 || m > 12 || d < 1) return false;
  static const uint8_t dim[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int max = dim[m - 1];
  if (m == 2 && ((y % 4 == 0 && y % 100 != 0) || y % 400 == 0)) max = 29;
  return d <= max;
}

bool alarmTargetTime(const Alarm& a, time_t& out) {
  if (a.kind != AK_ONCE && a.kind != AK_COUNTDOWN) return false;
  if (!validDate(a.year, a.month, a.day)) return false;
  struct tm d = {};
  d.tm_year = a.year - 1900; d.tm_mon = a.month - 1; d.tm_mday = a.day;
  d.tm_hour = a.hour; d.tm_min = a.minute; d.tm_sec = 0; d.tm_isdst = -1;
  out = mktime(&d);
  return true;
}

int alarmsCheck(const struct tm& now, int from) {
  for (int i = from < 0 ? 0 : from; i < nAlarms; i++) {
    const Alarm& a = list[i];
    if (!a.enabled) continue;
    if (a.hour != now.tm_hour || a.minute != now.tm_min) continue;
    switch (a.kind) {
      case AK_ONCE:
      case AK_COUNTDOWN:
        if (a.year == (uint16_t)(now.tm_year + 1900) &&
            a.month == (uint8_t)(now.tm_mon + 1) &&
            a.day == (uint8_t)now.tm_mday)
          return i;
        break;
      case AK_DAILY:
        return i;
      case AK_WEEKLY: {
        int bit = (now.tm_wday == 0) ? 6 : now.tm_wday - 1; // Mon-first
        if (a.weekdays & (1 << bit)) return i;
        break;
      }
    }
  }
  return -1;
}

bool alarmNextFire(int i, time_t& out) {
  if (i < 0 || i >= nAlarms) return false;
  const Alarm a = list[i];
  if (!a.enabled) return false;
  time_t now = time(nullptr);
  struct tm t; localtime_r(&now, &t);
  t.tm_hour = a.hour; t.tm_min = a.minute; t.tm_sec = 0;

  switch (a.kind) {
    case AK_ONCE:
    case AK_COUNTDOWN: {
      struct tm d = t;
      d.tm_year = a.year - 1900;
      d.tm_mon = a.month - 1;
      d.tm_mday = a.day;
      time_t ts = mktime(&d);
      out = ts;
      return ts >= now - 60;               // false = expired/past
    }
    case AK_DAILY: {
      time_t ts = mktime(&t);
      if (ts <= now) { t.tm_mday += 1; ts = mktime(&t); }
      out = ts;
      return true;
    }
    case AK_WEEKLY: {
      for (int d = 0; d < 8; d++) {
        struct tm c = t;
        c.tm_mday += d;
        time_t ts = mktime(&c);
        if (ts <= now) continue;
        struct tm chk; localtime_r(&ts, &chk);
        int bit = (chk.tm_wday == 0) ? 6 : chk.tm_wday - 1;
        if (a.weekdays & (1 << bit)) { out = ts; return true; }
      }
      return false;                        // no days selected
    }
  }
  return false;
}

String alarmNextIn(int i) {
  time_t ts;
  if (!alarmNextFire(i, ts)) return String("expired");
  long d = (long)(ts - time(nullptr));
  if (d < 0) d = 0;
  long days = d / 86400, hrs = (d % 86400) / 3600, mins = (d % 3600) / 60;
  char b[32];
  if (days > 0) snprintf(b, sizeof(b), "in %ldd %ldh", days, hrs);
  else if (hrs > 0) snprintf(b, sizeof(b), "in %ldh %ldm", hrs, mins);
  else snprintf(b, sizeof(b), "in %ldm", mins);
  return String(b);
}

// ---------------- snooze ----------------

void alarmSnooze(int minutes, const char* label, uint8_t melody, uint8_t type) {
  snzType = type;
  snzActive = true;
  snzAt = time(nullptr) + (time_t)(minutes * 60);
  strncpy(snzLabel, label ? label : "", sizeof(snzLabel) - 1);
  snzLabel[sizeof(snzLabel) - 1] = 0;
  snzMelody = melody;
}

bool snoozeActive() { return snzActive; }

bool snoozeFired() {
  if (!snzActive) return false;
  if (time(nullptr) >= snzAt) return true;
  return false;
}

const char* snoozeLabel() { return snzLabel; }
uint8_t snoozeMelody() { return snzMelody; }
uint8_t snoozeType() { return snzType; }

void snoozeClear() { snzActive = false; snzLabel[0] = 0; }

time_t snoozeRemaining() {
  if (!snzActive) return 0;
  time_t r = snzAt - time(nullptr);
  return (r > 0) ? r : 0;
}
