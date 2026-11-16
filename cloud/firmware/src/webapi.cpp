// ============================================================
//  webapi.cpp - WiFi web server: REST API + embedded web UI
//  (renamed from webserver.cpp: on Windows a project header
//  named webserver.h case-insensitively shadows the framework
//  <WebServer.h> and breaks the build - see include/webapi.h)
//
//  v3.20: alarms and tasks are addressed by their stable string
//  id, and every local change is queued for the Vercel cloud
//  (cloudsync.cpp). The JSON helpers now live in jsonutil.h so
//  the cloud client parses with exactly the same rules.
// ============================================================
#include <WebServer.h>
#include <ESPmDNS.h>
#include <WiFi.h>
#include "config.h"
#include "webapi.h"
#include "webui.h"
#include "webface.h"
#include "jsonutil.h"
#include "weather.h"
#include "widgets.h"
#include "app.h"
#include "faces.h"
#include "hands.h"
#include "audio.h"
#include "alarms.h"
#include "tasks.h"
#include "clocksync.h"
#include "settings.h"
#include "pomodoro.h"
#include "display.h"
#include "cloudsync.h"

static WebServer httpServer(80);

// ---------------- JSON helpers: see include/jsonutil.h ----------------
// (jFind / jInt / jStr / jBool / jNum / sanitizeLabel / parseDateStr,
//  shared with the cloud sync client - one dialect everywhere)

static void sendJson(int code, const String& body) {
  httpServer.send(code, "application/json", body);
}

// ---------------- status ----------------

static void addPomo(String& s) {
  PomodoroStatus p = pomoStatus();
  const char* st = p.state == PO_FOCUS ? "focus"
                 : p.state == PO_BREAK ? "break" : "idle";
  char b[168];
  snprintf(b, sizeof(b),
    ",\"pomodoro\":{\"state\":\"%s\",\"remaining\":%u,\"total\":%u,"
    "\"cycles\":%u,\"paused\":%s,\"focusMin\":%u,\"breakMin\":%u}",
    st, (unsigned)p.remaining, (unsigned)p.total, (unsigned)p.cycles,
    p.paused ? "true" : "false", (unsigned)pomoFocusMin(), (unsigned)pomoBreakMin());
  s += b;
}

static void handleStatus() {
  String s;
  s.reserve(1800);                                   // one allocation instead of many growing ones
  s = "{\"ok\":true,\"name\":\"";
  s += settings.name;
  s += "\",\"version\":\"" BUDDY_VERSION "\",\"time\":\"" + clockTimeStrSec() +
       "\",\"date\":\"" + clockDateStr() + "\",\"synced\":";
  s += clockSynced() ? "true" : "false";
  char b[860];
  bool online = WiFi.status() == WL_CONNECTED;
  int alarmsOn = 0;
  for (int i = 0; i < alarmsCount(); i++)
    if (alarmGet(i).enabled) alarmsOn++;
  Expression shown = appShownFace();
  const char* mode = app.mode == UI_FACE ? "face" : app.mode == UI_WIDGET ? "widgets"
                   : app.mode == UI_MENU ? "menu" : app.mode == UI_RINGING ? "ringing"
                   : app.mode == UI_NOTIFY ? "notify" : "sleep";
  snprintf(b, sizeof(b),
    ",\"ip\":\"%s\",\"rssi\":%d,\"heap\":%u,\"uptime\":%u,"
    "\"face\":\"%s\",\"faceIdx\":%d,\"mode\":\"%s\",\"autoFace\":%s,\"cuckoo\":%s,"
    "\"hour12\":%s,\"useF\":%s,\"widget\":%u,\"widgets\":%u,\"petting\":%s,"
    "\"pack\":%u,\"mood\":%u,\"faceVariant\":%u,\"gx\":%.2f,\"gy\":%.2f,"
    "\"volume\":%u,\"brightness\":%u,\"tz\":\"%s\",\"ntp\":\"%s\",\"ringing\":%s,"
    "\"ringType\":\"%s\",\"ringLabel\":\"%s\",\"snoozeIn\":%ld,"
    "\"alarmsOn\":%d,\"alarmsMax\":%d,\"alarms\":%d",
    online ? WiFi.localIP().toString().c_str() : "",
    online ? WiFi.RSSI() : 0, (unsigned)ESP.getFreeHeap(),
    (unsigned)(millis() / 1000),
    faceName(shown), (int)shown, mode,
    settings.autoFace ? "true" : "false", settings.cuckoo ? "true" : "false",
    settings.hour12 ? "true" : "false", settings.useF ? "true" : "false",
    (unsigned)app.widget, (unsigned)settings.widgets, app.petting ? "true" : "false",
    (unsigned)appCurPack(), (unsigned)appCurMood(), (unsigned)app.faceVariant,
    (double)app.rig.lookX.x, (double)app.rig.lookY.x,
    (unsigned)settings.volume, (unsigned)settings.brightness,
    settings.tz, clockServerInUse(), app.ringing ? "true" : "false",
    app.ringType == AT_REMINDER ? "reminder" : "alarm", app.ringLabel, (long)snoozeRemaining(),
    alarmsOn, MAX_ALARMS, alarmsCount());
  s += b;
  {                                   // do-not-disturb (Settings -> Buddy)
    char db[64];
    snprintf(db, sizeof(db), ",\"dnd\":{\"on\":%s,\"active\":%s}",
             settings.dnd ? "true" : "false", dndActiveNow() ? "true" : "false");
    s += db;
  }
  addPomo(s);
  {                                   // cloud sync (v3.20)
    char cb[160];
    uint32_t ago = cloudLastOkMs() ? (millis() - cloudLastOkMs()) / 1000UL : 0xFFFFFFFFUL;
    snprintf(cb, sizeof(cb),
      ",\"cloud\":{\"state\":\"%s\",\"rev\":%lu,\"queued\":%d,\"lastOkS\":%lu}",
      cloudStateName(), (unsigned long)cloudRev(), cloudQueuedOps(),
      (unsigned long)(ago == 0xFFFFFFFFUL ? 0 : ago));
    s += cb;
  }
  {
    Weather w = weatherGet();
    char wb[420];
    if (w.valid && weatherHasLocation())
      snprintf(wb, sizeof(wb),
        ",\"weather\":{\"ok\":true,\"temp\":%.1f,\"feels\":%.1f,\"hi\":%.1f,\"lo\":%.1f,\"code\":%d,"
        "\"cond\":\"%s\",\"rain\":%d,\"hum\":%d,\"wind\":%.0f,\"day\":%s,\"name\":\"%s\",\"age\":%lu}",
        (double)w.temp, (double)w.feels, (double)w.hi, (double)w.lo, w.code, weatherCondition(w.code),
        w.rain, w.humidity, (double)w.wind, w.isDay ? "true" : "false", settings.wxName,
        (unsigned long)((millis() - w.fetchedMs) / 1000UL));
    else
      snprintf(wb, sizeof(wb), ",\"weather\":{\"ok\":false,\"has\":%s,\"name\":\"%s\"}",
               weatherHasLocation() ? "true" : "false", settings.wxName);
    s += wb;
  }
  // next alarm (earliest)
  time_t best = 0; int bestIdx = -1;
  for (int i = 0; i < alarmsCount(); i++) {
    time_t ts;
    if (alarmGet(i).enabled && alarmNextFire(i, ts)) {
      if (bestIdx < 0 || ts < best) { best = ts; bestIdx = i; }
    }
  }
  if (bestIdx >= 0) {
    struct tm tmv; localtime_r(&best, &tmv);
    char tb[24], nb[240];
    strftime(tb, sizeof(tb), "%H:%M", &tmv);
    Alarm a = alarmGet(bestIdx);
    const char* kinds = a.kind == AK_ONCE ? "once" : a.kind == AK_DAILY ? "daily"
                      : a.kind == AK_WEEKLY ? "weekly" : "countdown";
    snprintf(nb, sizeof(nb),
      "\"nextAlarm\":{\"time\":\"%s\",\"label\":\"%s\",\"kind\":\"%s\",\"type\":\"%s\",\"in\":\"%s\"}",
      tb, a.label, kinds, a.type == AT_REMINDER ? "reminder" : "alarm", alarmNextIn(bestIdx).c_str());
    s += ",";
    s += nb;
  } else {
    s += ",\"nextAlarm\":null";
  }
  // tasks: today's tally (home card) + the rows the Tasks widget is showing (mirror)
  {
    char tb[560];
    int pos = 0;
    if (clockOk()) {
      time_t nt = time(nullptr);
      struct tm td; localtime_r(&nt, &td);
      int tTotal = 0, tDone = 0;
      tasksCountDay(td, tTotal, tDone);
      pos += snprintf(tb + pos, sizeof(tb) - pos, ",\"taskToday\":{\"n\":%d,\"done\":%d}", tTotal, tDone);
      struct tm wd;
      int wTotal = 0, wDone = 0;
      if (taskPickDay(wd, wTotal, wDone)) {
        char lab[12];
        taskDayLabel(wd, lab, sizeof(lab));
        int idx[MAX_TASKS];
        int n = tasksForDay(wd, idx, MAX_TASKS);
        int shown = (n > 3) ? 2 : n;                 // the widget trades its 3rd row for "+N more"
        pos += snprintf(tb + pos, sizeof(tb) - pos,
                        ",\"taskWidget\":{\"label\":\"%s\",\"total\":%d,\"done\":%d,\"rows\":[",
                        lab, wTotal, wDone);
        for (int r = 0; r < shown; r++) {
          Task t = taskGet(idx[r]);
          char ts[10] = "";
          if (t.hour != TASK_NO_TIME) {
            struct tm tt = {};
            tt.tm_hour = t.hour; tt.tm_min = t.minute;
            char hm[8], ap[4];
            clockParts(tt, hm, sizeof(hm), ap, sizeof(ap));
            if (ap[0]) snprintf(ts, sizeof(ts), "%s%c", hm, ap[0] == 'A' ? 'a' : 'p');
            else snprintf(ts, sizeof(ts), "%s", hm);
          }
          pos += snprintf(tb + pos, sizeof(tb) - pos, "%s[\"%s\",%d,\"%s\"]",
                          r ? "," : "", ts, t.done ? 1 : 0, t.title);
        }
        pos += snprintf(tb + pos, sizeof(tb) - pos, "],\"more\":%d}", (n > 3) ? n - 2 : 0);
      } else {
        pos += snprintf(tb + pos, sizeof(tb) - pos, ",\"taskWidget\":null");
      }
    } else {
      pos += snprintf(tb + pos, sizeof(tb) - pos, ",\"taskToday\":null,\"taskWidget\":null");
    }
    s += tb;
  }
  s += "}";
  sendJson(200, s);
}

// ---------------- alarms API (v3.20: entries are addressed by their stable id) ----------------

static const char* kindToStr(uint8_t k) {
  return k == AK_ONCE ? "once" : k == AK_DAILY ? "daily"
       : k == AK_WEEKLY ? "weekly" : "countdown";
}

static String alarmJson(int i, const Alarm& a) {
  char b[420];
  char date[24] = "";
  if (a.kind == AK_ONCE || a.kind == AK_COUNTDOWN)
    snprintf(date, sizeof(date), "%04u-%02u-%02u",
             (unsigned)a.year, (unsigned)a.month, (unsigned)a.day);
  time_t ts = 0;
  bool has = a.enabled && alarmNextFire(i, ts);
  bool expired = a.enabled && !has;
  snprintf(b, sizeof(b),
    "{\"id\":\"%s\",\"enabled\":%s,\"type\":\"%s\",\"kind\":\"%s\",\"time\":\"%02u:%02u\","
    "\"date\":\"%s\",\"weekdays\":%u,\"melody\":%u,\"label\":\"%s\","
    "\"in\":\"%s\",\"ts\":%lu,\"expired\":%s}",
    a.id, a.enabled ? "true" : "false", a.type == AT_REMINDER ? "reminder" : "alarm", kindToStr(a.kind),
    (unsigned)a.hour, (unsigned)a.minute, date,
    (unsigned)a.weekdays, (unsigned)a.melody, a.label,
    a.enabled ? alarmNextIn(i).c_str() : "-",
    has ? (unsigned long)ts : 0UL,
    expired ? "true" : "false");
  return String(b);
}

static void handleAlarmsGet() {
  String s = "{\"ok\":true,\"alarms\":[";
  int on = 0;
  for (int i = 0; i < alarmsCount(); i++) {
    if (i) s += ",";
    s += alarmJson(i, alarmGet(i));
    if (alarmGet(i).enabled) on++;
  }
  char tail[48];
  snprintf(tail, sizeof(tail), "],\"enabled\":%d,\"max\":%d}", on, MAX_ALARMS);
  s += tail;
  sendJson(200, s);
}

// parses an alarm body; "id" selects an existing entry (update) or is
// absent/unknown for a new one. Returns false on a bad payload.
bool parseAlarmBody(const String& body, Alarm& a, String& idOut) {
  idOut = "";
  String id, kind = "once", timeStr = "", date = "", label = "", type = "alarm";
  long melody = 0, weekdays = 0, minutes = 0, idNum = -1;
  bool enabled = true;
  jStr(body, "id", id);
  jStr(body, "kind", kind);
  jStr(body, "type", type);
  jStr(body, "time", timeStr);
  jStr(body, "date", date);
  jStr(body, "label", label);
  jInt(body, "melody", melody);
  jInt(body, "weekdays", weekdays);
  jInt(body, "minutes", minutes);
  jBool(body, "enabled", enabled);
  // legacy numeric id (pre v3.20 web UI cache): still accepted, resolved to
  // the entry at that index so an old tab keeps working after the OTA
  if (!id.length()) { jInt(body, "id", idNum); if (idNum >= 0 && idNum < alarmsCount()) id = alarmGet((int)idNum).id; }
  idOut = id;

  memset(&a, 0, sizeof(a));
  a.enabled = enabled ? 1 : 0;
  a.type = (type == "reminder") ? AT_REMINDER : AT_ALARM;
  if (a.type == AT_REMINDER)   // reminders use the notification tones
    a.melody = (melody >= MEL_FIRST_NOTIFY && melody < MEL_COUNT) ? (uint8_t)melody : (uint8_t)MEL_NOTIFY;
  else                          // alarms use the alarm tones
    a.melody = (melody >= 0 && melody < MEL_FIRST_NOTIFY) ? (uint8_t)melody : (uint8_t)MEL_CLASSIC;
  a.weekdays = (uint8_t)(weekdays & 0x7F);
  sanitizeLabel(label, a.label, sizeof(a.label));
  if (id.length() && id.length() <= ALARM_ID_LEN - 1)
    strncpy(a.id, id.c_str(), ALARM_ID_LEN - 1);

  if (kind == "daily") a.kind = AK_DAILY;
  else if (kind == "weekly") a.kind = AK_WEEKLY;
  else if (kind == "countdown") a.kind = AK_COUNTDOWN;
  else a.kind = AK_ONCE;

  // time "HH:MM"
  int h = 0, m = 0;
  if (timeStr.length() >= 4 && timeStr.indexOf(':') > 0) {
    h = timeStr.substring(0, timeStr.indexOf(':')).toInt();
    m = timeStr.substring(timeStr.indexOf(':') + 1).toInt();
  }
  if (h < 0 || h > 23 || m < 0 || m > 59) return false;
  a.hour = (uint8_t)h;
  a.minute = (uint8_t)m;

  int existing = id.length() ? alarmFind(id.c_str()) : -1;
  if (a.kind == AK_ONCE) {
    // date "YYYY-MM-DD"
    if (date.length() < 10) return false;
    a.year = (uint16_t)date.substring(0, 4).toInt();
    a.month = (uint8_t)date.substring(5, 7).toInt();
    a.day = (uint8_t)date.substring(8, 10).toInt();
    if (!validDate(a.year, a.month, a.day)) return false;     // Feb 30 etc. would never ring
  } else if (a.kind == AK_COUNTDOWN) {
    if (minutes >= 1 && minutes <= 527000L) {
      time_t t = time(nullptr) + (time_t)(minutes * 60L);
      if (t < 1000) return false;           // clock not set yet
      struct tm tmv; localtime_r(&t, &tmv);
      a.year = (uint16_t)(tmv.tm_year + 1900);
      a.month = (uint8_t)(tmv.tm_mon + 1);
      a.day = (uint8_t)tmv.tm_mday;
      a.hour = (uint8_t)tmv.tm_hour;
      a.minute = (uint8_t)tmv.tm_min;
    } else if (existing >= 0) {
      // update without new minutes (e.g. enable toggle): keep target date
      Alarm cur = alarmGet(existing);
      if (cur.kind != AK_COUNTDOWN) return false;
      a.year = cur.year; a.month = cur.month; a.day = cur.day;
    } else {
      return false;
    }
  } else if (a.kind == AK_WEEKLY) {
    if (a.weekdays == 0) return false;
  }
  return true;
}

static void handleAlarmPost() {
  String body = httpServer.arg("plain");
  Alarm a;
  String id;
  if (!parseAlarmBody(body, a, id)) {
    sendJson(400, "{\"ok\":false,\"error\":\"bad alarm payload\"}");
    return;
  }
  int idx = id.length() ? alarmFind(id.c_str()) : -1;
  if (idx >= 0) {
    alarmUpdate(idx, a);
    cloudQueueAlarmPut(a);
    sendJson(200, alarmJson(idx, a));
  } else {
    if (alarmsCount() >= MAX_ALARMS) {
      sendJson(400, "{\"ok\":false,\"error\":\"alarm list full\"}");
      return;
    }
    int nIdx = alarmAdd(a);
    if (nIdx < 0) {
      sendJson(400, "{\"ok\":false,\"error\":\"alarm list full\"}");
      return;
    }
    Alarm stored = alarmGet(nIdx);
    cloudQueueAlarmPut(stored);
    sendJson(200, alarmJson(nIdx, stored));
  }
  cloudRequestSync();
}

static void handleAlarmDelete() {
  String id = "";
  for (uint8_t i = 0; i < httpServer.args(); i++)
    if (httpServer.argName(i) == "id") id = httpServer.arg(i);
  int idx = id.length() ? alarmFind(id.c_str()) : -1;
  // legacy numeric id from an old cached web UI (digits only - a failed
  // toInt() on a string id must never fall through to index 0)
  if (idx < 0 && id.length()) {
    bool numeric = id.length() > 0;
    for (size_t i = 0; i < id.length() && numeric; i++)
      if (id[i] < '0' || id[i] > '9') numeric = false;
    if (numeric) {
      long n = id.toInt();
      if (n >= 0 && n < alarmsCount()) idx = (int)n;
    }
  }
  if (idx >= 0 && alarmRemove(idx)) {
    cloudQueueAlarmDel(id.c_str());
    cloudRequestSync();
    sendJson(200, "{\"ok\":true}");
  } else
    sendJson(400, "{\"ok\":false,\"error\":\"bad id\"}");
}

static void handleAlarmAction() {
  String body = httpServer.arg("plain");
  String action = "";
  jStr(body, "action", action);
  long melody = 0;
  jInt(body, "melody", melody);
  if (action == "dismiss") {
    appStopRinging(false);
    sendJson(200, "{\"ok\":true}");
  } else if (action == "snooze") {
    appStopRinging(true);
    sendJson(200, "{\"ok\":true}");
  } else if (action == "test") {
    if (melody >= 0 && melody < MEL_COUNT) {
      if (melody >= MEL_FIRST_NOTIFY) audioPlayNotify((MelodyId)melody);   // buzz + ding + tone
      else audioPlay((MelodyId)melody, false);
      sendJson(200, "{\"ok\":true}");
    } else sendJson(400, "{\"ok\":false,\"error\":\"bad melody\"}");
  } else if (action == "stop") {
    audioStop();
    sendJson(200, "{\"ok\":true}");
  } else if (action == "notify") {          // preview a reminder on the device right now
    String label = "";
    jStr(body, "label", label);
    char lb[ALARM_LABEL_LEN];
    sanitizeLabel(label, lb, sizeof(lb));
    appWake();
    appStartNotify(-1, lb, (melody >= MEL_FIRST_NOTIFY && melody < MEL_COUNT) ? (uint8_t)melody : (uint8_t)MEL_NOTIFY);
    sendJson(200, "{\"ok\":true}");
  } else {
    sendJson(400, "{\"ok\":false,\"error\":\"unknown action\"}");
  }
}

// ---------------- tasks API (v3.20: entries are addressed by their stable id) ----------------
// tasks are quiet checklist items: {id, title, date, end, time, done}
// date = first day ("YYYY-MM-DD"), end = optional last day, time = optional "HH:MM".

static String taskJson(int i, const Task& t) {
  char b[340];
  char date[16], end[16], time[8];
  snprintf(date, sizeof(date), "%04u-%02u-%02u", (unsigned)t.year, (unsigned)t.month, (unsigned)t.day);
  if (t.hasEnd) snprintf(end, sizeof(end), "%04u-%02u-%02u", (unsigned)t.endYear, (unsigned)t.endMonth, (unsigned)t.endDay);
  else end[0] = 0;
  if (t.hour != TASK_NO_TIME) snprintf(time, sizeof(time), "%02u:%02u", (unsigned)t.hour, (unsigned)t.minute);
  else time[0] = 0;
  snprintf(b, sizeof(b),
    "{\"id\":\"%s\",\"title\":\"%s\",\"date\":\"%s\",\"end\":\"%s\",\"time\":\"%s\",\"done\":%s}",
    t.id, t.title, date, end, time, t.done ? "true" : "false");
  return String(b);
}

static void handleTasksGet() {
  String s = "{\"ok\":true,\"tasks\":[";
  for (int i = 0; i < tasksCount(); i++) {
    if (i) s += ",";
    s += taskJson(i, taskGet(i));
  }
  char tail[40];
  snprintf(tail, sizeof(tail), "],\"max\":%d}", MAX_TASKS);
  s += tail;
  sendJson(200, s);
}

// v3.21: the task parsing lives in its own function so the USB command
// channel (src/serialcmd.cpp) accepts exactly the same task bodies.
bool parseTaskBody(const String& body, Task& t, String& idOut, String& err) {
  String id, title, date, end, timeStr;
  bool done = false;
  jStr(body, "id", id);
  jStr(body, "title", title);
  jStr(body, "date", date);
  jStr(body, "end", end);
  jStr(body, "time", timeStr);
  jBool(body, "done", done);
  // legacy numeric id (pre v3.20 cached web UI): resolve by index
  if (!id.length()) {
    long idNum = -1;
    if (jInt(body, "id", idNum) && idNum >= 0 && idNum < tasksCount())
      id = taskGet((int)idNum).id;
  }
  idOut = id;

  memset(&t, 0, sizeof(t));
  sanitizeLabel(title, t.title, sizeof(t.title));
  if (!t.title[0]) { err = "a task needs a title (letters, numbers and basic symbols only)"; return false; }
  if (id.length() && id.length() <= TASK_ID_LEN - 1)
    strncpy(t.id, id.c_str(), TASK_ID_LEN - 1);
  uint16_t y; uint8_t mo, da;                         // packed fields cannot bind to references
  if (!parseDateStr(date, y, mo, da)) { err = "bad date"; return false; }
  t.year = y; t.month = mo; t.day = da;
  if (end.length() >= 10) {
    uint32_t ke = 0;
    uint16_t ey; uint8_t emo, eda;
    if (!parseDateStr(end, ey, emo, eda) ||
        (ke = (uint32_t)ey * 10000UL + (uint32_t)emo * 100UL + eda) <
        (uint32_t)y * 10000UL + (uint32_t)mo * 100UL + da) {
      err = "end is before the start"; return false;
    }
    t.endYear = ey; t.endMonth = emo; t.endDay = eda;
    t.hasEnd = 1;
  }
  int h = -1, m = 0;
  if (timeStr.length() >= 4 && timeStr.indexOf(':') > 0) {
    h = timeStr.substring(0, timeStr.indexOf(':')).toInt();
    m = timeStr.substring(timeStr.indexOf(':') + 1).toInt();
  }
  if (h < 0 || h > 23 || m < 0 || m > 59) { t.hour = TASK_NO_TIME; t.minute = 0; }
  else { t.hour = (uint8_t)h; t.minute = (uint8_t)m; }
  t.done = done ? 1 : 0;
  return true;
}

static void handleTaskPost() {
  String body = httpServer.arg("plain");
  Task t;
  String id, err;
  if (!parseTaskBody(body, t, id, err)) {
    sendJson(400, "{\"ok\":false,\"error\":\"" + err + "\"}");
    return;
  }

  int idx = id.length() ? taskFind(id.c_str()) : -1;
  if (idx >= 0) {
    taskUpdate(idx, t);
    cloudQueueTaskPut(t);
    sendJson(200, taskJson(idx, t));
  } else {
    if (tasksCount() >= MAX_TASKS) {
      sendJson(400, "{\"ok\":false,\"error\":\"task list full\"}");
      return;
    }
    int nIdx = taskAdd(t);
    if (nIdx < 0) {
      sendJson(400, "{\"ok\":false,\"error\":\"task list full\"}");
      return;
    }
    Task stored = taskGet(nIdx);
    cloudQueueTaskPut(stored);
    sendJson(200, taskJson(nIdx, stored));
  }
  cloudRequestSync();
}

static void handleTaskDelete() {
  String id = "";
  for (uint8_t i = 0; i < httpServer.args(); i++)
    if (httpServer.argName(i) == "id") id = httpServer.arg(i);
  int idx = id.length() ? taskFind(id.c_str()) : -1;
  // legacy numeric id from an old cached web UI (digits only)
  if (idx < 0 && id.length()) {
    bool numeric = id.length() > 0;
    for (size_t i = 0; i < id.length() && numeric; i++)
      if (id[i] < '0' || id[i] > '9') numeric = false;
    if (numeric) {
      long n = id.toInt();
      if (n >= 0 && n < tasksCount()) idx = (int)n;
    }
  }
  if (idx >= 0 && taskRemove(idx)) {
    cloudQueueTaskDel(id.c_str());
    cloudRequestSync();
    sendJson(200, "{\"ok\":true}");
  } else
    sendJson(400, "{\"ok\":false,\"error\":\"bad id\"}");
}

// ---------------- other endpoints ----------------

static void handlePomodoro() {
  String body = httpServer.arg("plain");
  String action = "";
  jStr(body, "action", action);
  if (action == "start") { pomoStart(); appPomoToFront(); }   // (v3.13) jump to the face screen so the timer is visible
  else if (action == "pause") pomoPause();
  else if (action == "reset") pomoReset();
  else if (action == "skip") pomoSkip();
  else if (action == "lens") {
    long f = 25, b = 5;
    jInt(body, "focus", f);
    jInt(body, "brk", b);
    pomoSetLens((uint16_t)f, (uint16_t)b);
  } else {
    sendJson(400, "{\"ok\":false,\"error\":\"unknown action\"}");
    return;
  }
  sendJson(200, "{\"ok\":true}");
}

// v3.21: a face may carry a VARIANT (a deterministic tweak of the
// expression's parameters - 32 of them per expression, so 41 expressions
// x 32 variants x 6 eye packs x 7 moods are all addressable) and a
// TRANSIENT eye style / personality.  The overrides are not saved: the
// user's own settings are never changed by a host.
static void handleFace() {
  String body = httpServer.arg("plain");
  String f = "";
  if (jStr(body, "face", f)) {
    int idx = faceIndexByName(f.c_str());
    if (idx < 0) {
      sendJson(400, "{\"ok\":false,\"error\":\"unknown face\"}");
      return;
    }
    long variant = 0, pack = -1, mood = -1;
    jInt(body, "variant", variant);
    jInt(body, "pack", pack);
    jInt(body, "mood", mood);
    appWake();                       // in case it was asleep
    app.faceVariant = (uint8_t)(variant < 0 ? 0 : (variant % VARIANT_COUNT));
    app.packOv = (int8_t)((pack >= 0 && pack < PK_COUNT) ? pack : -1);
    app.moodOv = (int8_t)((mood >= 0 && mood < MD_COUNT) ? mood : -1);
    // the face is three parts: the EYES (the expression), the MOUTH and
    // the HANDS.  The flash holds how each part behaves; the host sends
    // the combination.
    String hands, mouth;
    jStr(body, "hands", hands);
    jStr(body, "mouth", mouth);
    if (hands.length()) {
      int h = handIndexByName(hands.c_str());
      if (h < HP_COUNT) app.handPose = (uint8_t)h;
    }
    if (mouth.length()) {
      int m = mouthIndexByName(mouth.c_str());
      app.mouthOv = (int8_t)(m >= 0 ? m : -1);
    }
    app.ovAt = millis();
    appSetFace((Expression)idx);
    app.nextAutoFace = millis() + 30000;   // 30s grace before auto takes back
    sendJson(200, "{\"ok\":true,\"variant\":" + String((int)app.faceVariant) + "}");
  } else {
    sendJson(400, "{\"ok\":false,\"error\":\"face required\"}");
  }
}

static bool validTz(const String& tz) {
  if (tz.length() < 3 || tz.length() > 40) return false;
  for (size_t i = 0; i < tz.length(); i++) {
    char c = tz[i];
    bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') || strchr("<>+-/:,. ", c);
    if (!ok) return false;
  }
  return true;
}

static void handleSettingsGet() {
  char b[620];
  snprintf(b, sizeof(b),
    "{\"ok\":true,\"volume\":%u,\"brightness\":%u,\"autoFace\":%s,\"cuckoo\":%s,"
    "\"hour12\":%s,\"useF\":%s,\"widgets\":%u,\"wxLat\":%.4f,\"wxLon\":%.4f,\"wxName\":\"%s\","
    "\"pack\":%u,\"mood\":%u,"
    "\"name\":\"%s\",\"tz\":\"%s\",\"melodies\":%d,"
    "\"dnd\":%s,\"dndStart\":\"%02u:%02u\",\"dndEnd\":\"%02u:%02u\"}",
    (unsigned)settings.volume, (unsigned)settings.brightness,
    settings.autoFace ? "true" : "false", settings.cuckoo ? "true" : "false",
    settings.hour12 ? "true" : "false", settings.useF ? "true" : "false", (unsigned)settings.widgets,
    (double)settings.wxLat, (double)settings.wxLon, settings.wxName,
    (unsigned)settings.pack, (unsigned)settings.mood,
    settings.name, settings.tz, MEL_COUNT,
    settings.dnd ? "true" : "false",
    (unsigned)(settings.dndStart / 60), (unsigned)(settings.dndStart % 60),
    (unsigned)(settings.dndEnd / 60), (unsigned)(settings.dndEnd % 60));
  sendJson(200, b);
}

// "HH:MM" -> minutes from midnight, -1 when malformed
static long parseHM(const String& s) {
  int c = s.indexOf(':');
  if (c <= 0) return -1;
  long h = s.substring(0, c).toInt(), m = s.substring(c + 1).toInt();
  if (h < 0 || h > 23 || m < 0 || m > 59) return -1;
  return h * 60 + m;
}

static void handleSettingsPost() {
  String body = httpServer.arg("plain");
  long v = -1;
  if (jInt(body, "volume", v) && v >= 0 && v <= 10) {
    settings.volume = (uint8_t)v;
    audioSetVolume(settings.volume);
  }
  if (jInt(body, "brightness", v) && v >= 40 && v <= 255) {
    settings.brightness = (uint8_t)v;
    displaySetBrightness(settings.brightness);
  }
  bool bv;
  if (jBool(body, "autoFace", bv)) settings.autoFace = bv;
  if (jBool(body, "cuckoo", bv)) settings.cuckoo = bv;
  if (jBool(body, "hour12", bv)) settings.hour12 = bv;
  if (jBool(body, "useF", bv)) settings.useF = bv;
  {                                   // do-not-disturb window ("HH:MM" from the web app)
    if (jBool(body, "dnd", bv)) settings.dnd = bv;
    String hm;
    long m;
    if (jStr(body, "dndStart", hm) && (m = parseHM(hm)) >= 0) settings.dndStart = (uint16_t)m;
    if (jStr(body, "dndEnd", hm) && (m = parseHM(hm)) >= 0) settings.dndEnd = (uint16_t)m;
  }
  if (jInt(body, "widgets", v) && (v & WG_ALL_MASK) != 0) {
    settings.widgets = (uint8_t)(v & WG_ALL_MASK);
    if (!(settings.widgets & (1 << app.widget))) app.widget = widgetNext(app.widget, settings.widgets);
  }
  {                                   // weather location (picked in the web app via the geocoding search)
    double la, lo; String wn;
    if (jNum(body, "wxLat", la) && jNum(body, "wxLon", lo) && jStr(body, "wxName", wn) &&
        la >= -90 && la <= 90 && lo >= -180 && lo <= 180) {
      settings.wxLat = (float)la; settings.wxLon = (float)lo;
      sanitizeLabel(wn, settings.wxName, sizeof(settings.wxName));
      weatherRefresh();
    }
    if (jBool(body, "wxClear", bv) && bv) { settings.wxName[0] = 0; settings.wxLat = settings.wxLon = 0; }
  }
  if (jInt(body, "pack", v) && v >= 0 && v < PK_COUNT) settings.pack = (uint8_t)v;
  if (jInt(body, "mood", v) && v >= 0 && v < MD_COUNT && v != settings.mood) {
    settings.mood = (uint8_t)v;
    appStyleChanged();                 // new personality: new home face + schedules
  }
  String s;
  if (jStr(body, "name", s)) sanitizeLabel(s, settings.name, sizeof(settings.name));
  if (jStr(body, "tz", s) && validTz(s)) {
    strncpy(settings.tz, s.c_str(), sizeof(settings.tz) - 1);
    settings.tz[sizeof(settings.tz) - 1] = 0;
    clockSetTz(settings.tz);
  }
  settingsSave();
  handleSettingsGet();
}

static void handleAction() {
  String body = httpServer.arg("plain");
  String a = "";
  jStr(body, "a", a);
  if (a == "reboot") {
    sendJson(200, "{\"ok\":true}");
    delay(150);
    ESP.restart();
  }
  if (appAction(a.c_str())) sendJson(200, "{\"ok\":true}");
  else sendJson(400, "{\"ok\":false,\"error\":\"unknown action\"}");
}

static void handleRoot() {
  httpServer.sendHeader("Cache-Control", "no-cache, must-revalidate");
  httpServer.send_P(200, PSTR("text/html; charset=utf-8"), INDEX_HTML);
}

static void handleFaceJs() {
  httpServer.sendHeader("Cache-Control", "no-cache, must-revalidate");
  httpServer.send_P(200, PSTR("application/javascript; charset=utf-8"), FACE_JS);
}

static void handleNotFound() {
  httpServer.sendHeader("Location", "/");
  httpServer.send(302, "text/plain", "");
}

// ---------------- begin ----------------

static bool serverStarted = false;

// routes can be registered any time; the listening socket is only opened once WiFi is up
void webBegin() {
  httpServer.on("/", HTTP_GET, handleRoot);
  httpServer.on("/face.js", HTTP_GET, handleFaceJs);
  httpServer.on("/api/status", HTTP_GET, handleStatus);
  httpServer.on("/api/alarms", HTTP_GET, handleAlarmsGet);
  httpServer.on("/api/alarm", HTTP_POST, handleAlarmPost);
  httpServer.on("/api/alarm", HTTP_DELETE, handleAlarmDelete);
  httpServer.on("/api/alarm/action", HTTP_POST, handleAlarmAction);
  httpServer.on("/api/tasks", HTTP_GET, handleTasksGet);
  httpServer.on("/api/tasks", HTTP_POST, handleTaskPost);
  httpServer.on("/api/tasks", HTTP_DELETE, handleTaskDelete);
  httpServer.on("/api/pomodoro", HTTP_POST, handlePomodoro);
  httpServer.on("/api/face", HTTP_POST, handleFace);
  httpServer.on("/api/settings", HTTP_GET, handleSettingsGet);
  httpServer.on("/api/settings", HTTP_POST, handleSettingsPost);
  httpServer.on("/api/action", HTTP_POST, handleAction);
  httpServer.onNotFound(handleNotFound);
}

void webStart() {
  if (!serverStarted) {
    httpServer.begin();
    serverStarted = true;
    Serial.println("[WEB] HTTP server started");
  }
  MDNS.end();                                   // (re)announce after every reconnect
  if (MDNS.begin(MDNS_HOST)) {
    MDNS.addService("http", "tcp", 80);
    Serial.println("[WEB] http://" MDNS_HOST ".local");
  }
}

void webTick() {
  if (serverStarted) httpServer.handleClient();
}
