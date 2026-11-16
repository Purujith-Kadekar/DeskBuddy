// ============================================================
//  serialcmd.cpp - the USB CDC command channel (v3.21)
//
//  See include/serialcmd.h for the protocol.  This file only READS the
//  USB port and calls the same functions the REST API calls, so the two
//  channels cannot drift apart.
//
//  Nothing here removes or switches off a feature: it sets a face,
//  plays an action, shows a notification, or edits the planner - all of
//  which the web app can do too.
// ============================================================
#include <Arduino.h>
#include "config.h"
#include "serialcmd.h"
#include "webapi.h"
#include "jsonutil.h"
#include "app.h"
#include "faces.h"
#include "hands.h"
#include "audio.h"
#include "alarms.h"
#include "tasks.h"
#include "settings.h"
#include "pomodoro.h"
#include "cloudsync.h"
#include "clocksync.h"
#include "display.h"
#include "widgets.h"

static String s_line;
static bool s_overflow = false;
static uint32_t s_lastCmdMs = 0;

static void ok(const String& detail = "") {
  Serial.printf("[CMD] ok %s\n", detail.c_str());
}
static void err(const String& reason) {
  Serial.printf("[CMD] err %s\n", reason.c_str());
}

// The buddy's own answer to STATUS - one line, so a host can parse it.
static void printStatus() {
  Serial.printf("[CMD] status {\"face\":\"%s\",\"mode\":\"%d\",\"widgets\":%d,"
                "\"alarms\":%d,\"tasks\":%d,\"time\":\"%s\"}\n",
                faceName(appShownFace()), (int)app.mode, (int)settings.widgets,
                alarmsCount(), tasksCount(), clockTimeStrSec().c_str());
}

// ---------------------------------------------------------------- commands

// FACE <eyes>[|<mouth>[|<hands>[|<variant>]]]
// The face is three parts: the eyes (an expression name), the mouth (a
// mouth name) and the hands (a hand pose).  The flash holds how each
// part behaves; the host sends the combination.  The variant is a
// deterministic tweak of the expression (0..31) and the pack / mood are
// TRANSIENT overrides - the saved eye style and personality are never
// changed by a host.
static void cmdFace(const String& arg) {
  String rest = arg;
  rest.trim();
  if (!rest.length()) { err("face needs a token"); return; }
  long variant = 0;
  String mouth, hands;
  int bar = rest.indexOf('|');
  if (bar >= 0) {
    String tail = rest.substring(bar + 1);
    rest = rest.substring(0, bar);
    int bar2 = tail.indexOf('|');
    if (bar2 >= 0) {
      String h = tail.substring(bar2 + 1);
      tail = tail.substring(0, bar2);
      int bar3 = h.indexOf('|');
      if (bar3 >= 0) { variant = h.substring(bar3 + 1).toInt(); h = h.substring(0, bar3); }
      hands = h;
    }
    mouth = tail;
  }
  rest.trim();
  int idx = faceIndexByName(rest.c_str());
  if (idx < 0) { err("unknown face " + rest); return; }
  appWake();
  app.faceVariant = (uint8_t)(variant < 0 ? 0 : (variant % VARIANT_COUNT));
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
  app.nextAutoFace = millis() + 30000;
  ok(rest);
}

static void cmdAction(const String& arg) {
  String a = arg;
  a.trim();
  if (!a.length()) { err("action needs a name"); return; }
  if (!appAction(a.c_str())) { err("unknown action " + a); return; }
  ok(a);
}

static void cmdNotify(const String& arg) {
  // NOTIFY <label>|<melody>
  String rest = arg;
  uint8_t melody = MEL_NOTIFY;
  int bar = rest.lastIndexOf('|');
  if (bar >= 0) {
    String m = rest.substring(bar + 1);
    rest = rest.substring(0, bar);
    long mv = m.toInt();
    if (mv >= 0 && mv < MEL_COUNT) melody = (uint8_t)mv;
  }
  char lb[ALARM_LABEL_LEN];
  sanitizeLabel(rest, lb, sizeof(lb));
  appWake();
  appStartNotify(-1, lb, melody);
  ok(rest);
}

static void cmdDismiss(bool snooze) {
  appStopRinging(snooze);
  ok(snooze ? "snoozed" : "dismissed");
}

static void cmdMelody(const String& arg) {
  String a = arg;                 // trim() is not const on this core
  a.trim();
  long mv = a.toInt();
  if (mv < 0 || mv >= MEL_COUNT) { err("melody must be 0..8"); return; }
  if (mv >= MEL_FIRST_NOTIFY) audioPlayNotify((MelodyId)mv);
  else audioPlay((MelodyId)mv, false);
  ok(String(mv));
}

static void cmdAlarm(const String& body) {
  Alarm a;
  String id;
  if (!parseAlarmBody(body, a, id)) { err("bad alarm payload"); return; }
  int existing = id.length() ? alarmFind(id.c_str()) : -1;
  if (existing >= 0) {
    alarmUpdate(existing, a);
    cloudQueueAlarmPut(a);
    ok("updated " + String(a.id));
  } else {
    if (alarmsCount() >= alarmsCapacity()) { err("alarm list full"); return; }
    int n = alarmAdd(a);
    if (n < 0) { err("alarm list full"); return; }
    Alarm stored = alarmGet(n);
    cloudQueueAlarmPut(stored);
    ok("added " + String(stored.id));
  }
  cloudRequestSync();
}

static void cmdTask(const String& body) {
  Task t;
  String id, e;
  if (!parseTaskBody(body, t, id, e)) { err(e.length() ? e : String("bad task")); return; }
  int existing = id.length() ? taskFind(id.c_str()) : -1;
  if (existing >= 0) {
    taskUpdate(existing, t);
    cloudQueueTaskPut(t);
    ok("updated " + String(t.id));
  } else {
    if (tasksCount() >= tasksCapacity()) { err("task list full"); return; }
    int n = taskAdd(t);
    if (n < 0) { err("task list full"); return; }
    Task stored = taskGet(n);
    cloudQueueTaskPut(stored);
    ok("added " + String(stored.id));
  }
  cloudRequestSync();
}

static void cmdDel(const String& arg) {
  // DEL <alarm|task> <id>
  String rest = arg;
  rest.trim();
  int sp = rest.indexOf(' ');
  if (sp < 0) { err("del needs a kind and an id"); return; }
  String kind = rest.substring(0, sp);
  String id = rest.substring(sp + 1);
  id.trim();
  if (kind == "alarm") {
    int idx = alarmFind(id.c_str());
    if (idx < 0) { err("no such alarm"); return; }
    Alarm a = alarmGet(idx);
    alarmRemove(idx);
    cloudQueueAlarmDel(a.id);
    ok("deleted " + id);
  } else if (kind == "task") {
    int idx = taskFind(id.c_str());
    if (idx < 0) { err("no such task"); return; }
    Task t = taskGet(idx);
    taskRemove(idx);
    cloudQueueTaskDel(t.id);
    ok("deleted " + id);
  } else {
    err("del needs alarm or task");
  }
  cloudRequestSync();
}

static void cmdPomo(const String& arg) {
  String a = arg;
  a.trim();
  if (a == "start") { pomoStart(); appPomoToFront(); ok(a); }
  else if (a == "pause") { pomoPause(); ok(a); }
  else if (a == "reset") { pomoReset(); ok(a); }
  else if (a == "skip") { pomoSkip(); ok(a); }
  else err("unknown pomodoro action " + a);
}

static void cmdSet(const String& body) {
  // the same settings the web app writes; every key is optional
  long v = 0;
  bool b = false;
  String s;
  if (jInt(body, "volume", v) && v >= 0 && v <= 10) {
    settings.volume = (uint8_t)v; audioSetVolume(settings.volume);
  }
  if (jInt(body, "brightness", v) && v >= 40 && v <= 255) {
    settings.brightness = (uint8_t)v; displaySetBrightness(settings.brightness);
  }
  if (jBool(body, "autoFace", b)) settings.autoFace = b;
  if (jBool(body, "cuckoo", b)) settings.cuckoo = b;
  if (jBool(body, "hour12", b)) settings.hour12 = b;
  if (jBool(body, "useF", b)) settings.useF = b;
  if (jBool(body, "dnd", b)) settings.dnd = b;
  if (jStr(body, "name", s)) sanitizeLabel(s, settings.name, sizeof(settings.name));
  if (jInt(body, "pack", v) && v >= 0 && v < PK_COUNT) {
    settings.pack = (uint8_t)v; appStyleChanged();
  }
  if (jInt(body, "mood", v) && v >= 0 && v < MD_COUNT && v != settings.mood) {
    settings.mood = (uint8_t)v; appStyleChanged();
  }
  settingsSave();
  ok("settings updated");
}

// ------------------------------------------------------------- dispatching
static void handleLine(char* line) {
  String s(line);
  s.trim();
  if (!s.length()) return;

  int sp = s.indexOf(' ');
  String verb = (sp >= 0) ? s.substring(0, sp) : s;
  String arg = (sp >= 0) ? s.substring(sp + 1) : "";
  verb.toUpperCase();

  if (verb == "PING") { ok("pong"); return; }
  if (verb == "STATUS") { printStatus(); return; }
  if (verb == "FACE") { cmdFace(arg); return; }
  if (verb == "ACT") { cmdAction(arg); return; }
  if (verb == "NOTIFY") { cmdNotify(arg); return; }
  if (verb == "DISMISS") { cmdDismiss(false); return; }
  if (verb == "SNOOZE") { cmdDismiss(true); return; }
  if (verb == "MELODY") { cmdMelody(arg); return; }
  if (verb == "ALARM") { cmdAlarm(arg); return; }
  if (verb == "TASK") { cmdTask(arg); return; }
  if (verb == "DEL") { cmdDel(arg); return; }
  if (verb == "POMO") { cmdPomo(arg); return; }
  if (verb == "SET") { cmdSet(arg); return; }
  err("unknown command " + verb);
}

void serialCmdBegin() {
  s_line.reserve(96);
  s_overflow = false;
  s_lastCmdMs = millis();
}

void serialCmdTick() {
  // read whatever the host sent; the port is also the telemetry channel,
  // so this coexists with the logging without touching it
  while (Serial.available() > 0) {
    int c = Serial.read();
    if (c < 0) break;
    if (c == '\r') continue;
    if (c != '\n') {
      if (s_line.length() < 200) s_line += (char)c;
      else s_overflow = true;
      continue;
    }
    // end of a line
    if (s_overflow) { err("line too long"); s_line = ""; s_overflow = false; continue; }
    if (s_line.length()) {
      handleLine(s_line.begin());
      s_line = "";
    }
  }
  s_lastCmdMs = millis();
}
