// ============================================================
//  VentunoQ_PlannerClient.ino - planner client for the Ventuno Q
//
//  The Ventuno Q shares the SAME planner as the DeskBuddy:
//  the Vercel app is the single source of truth, and this sketch
//  is the reference client for pulling tasks / reminders /
//  alarms and pushing edits.
//
//  ENDPOINT (the answer to "what is the endpoint for the Ventuno Q"):
//
//    GET  https://YOUR-APP.vercel.app/api/sync?dev=ventunoq&rev=N
//         -> 200 {"ok":true,"rev":N,"tasks":[...],"alarms":[...]}
//         -> 304 (nothing changed since rev N - poll cost: 0 reads)
//
//    POST https://YOUR-APP.vercel.app/api/sync
//         body: {"dev":"ventunoq","baseRev":N,"ops":[
//            {"op":"alarm.put","id":"l...","alarm":{...}},
//            {"op":"alarm.del","id":"l..."},
//            {"op":"task.put","id":"l...","task":{...}},
//            {"op":"task.del","id":"l..."} ]}
//         -> 200 full new state (1 read + 1 write, whole document)
//         -> 409 conflict: someone else wrote first; the body holds
//            the fresh state - apply it locally, then re-send the ops
//
//  Entry shapes (identical on both devices, the web app and the API):
//    alarm: {"id","enabled","type":"alarm|reminder","kind":"once|daily|weekly|countdown",
//            "time":"HH:MM","date":"YYYY-MM-DD","weekdays":0..127,"melody":0..8,"label"}
//    task : {"id","title","date","YYYY-MM-DD","end":"YYYY-MM-DD","time":"HH:MM","done":bool}
//
//  Wire this in like the DeskBuddy does (see its src/cloudsync.cpp for the
//  production version): poll every 30 s, keep a local copy, and run the
//  alarm checks against the local clock (NTP) so rings never depend on WiFi.
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include <ArduinoJson.h>        // Ventuno Q side: ArduinoJson is fine to use

// ---- your settings ----
const char* WIFI_SSID = "YourWiFiName";
const char* WIFI_PASS = "YourWiFiPassword";
const char* SYNC_BASE = "https://YOUR-APP.vercel.app";   // no trailing slash
const char* SYNC_KEY  = "";                              // X-Api-Key (SYNC_API_KEY); "" = off

// ---- planner cache (the local mirror of the cloud document) ----
const int   MAX_ENTRIES = 32;
const char* DEVICE_NAME = "ventunoq";

struct AlarmEntry {
  char     id[13];
  bool     enabled;
  bool     isReminder;
  uint8_t  kind;          // 0 once, 1 daily, 2 weekly, 3 countdown
  uint8_t  hour, minute;
  uint16_t year; uint8_t month, day;      // once / countdown target
  uint8_t  weekdays;      // bit0=Mon .. bit6=Sun
  uint8_t  melody;
  char     label[32];
};
struct TaskEntry {
  char     id[13];
  char     title[24];
  uint16_t year; uint8_t month, day;      // first day
  bool     hasEnd;
  uint16_t endYear; uint8_t endMonth, endDay;
  bool     hasTime; uint8_t hour, minute;
  bool     done;
};

AlarmEntry alarms[MAX_ENTRIES]; int nAlarms = 0;
TaskEntry  tasks[MAX_ENTRIES];  int nTasks  = 0;
uint32_t   rev = 0;                       // the revision we are standing on

// ------------------------------------------------------------
//  HTTP helpers
// ------------------------------------------------------------

// GET the state if the server has a newer rev. Returns:
//   0 - not changed (304)
//   1 - changed, cache updated
//  -1 - network / server error
int syncPoll() {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client;
  client.setInsecure();                   // same policy as DeskBuddy's weather client
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(7000);
  http.setTimeout(12000);
  String url = String(SYNC_BASE) + "/api/sync?dev=" + DEVICE_NAME + "&rev=" + String(rev);
  if (!http.begin(client, url)) return -1;
  if (strlen(SYNC_KEY)) http.addHeader("X-Api-Key", SYNC_KEY);
  int code = http.GET();
  if (code == 304) { http.end(); return 0; }
  if (code != 200) { Serial.printf("[SYNC] GET %d\n", code); http.end(); return -1; }
  String body = http.getString();
  http.end();
  if (!parseState(body)) return -1;
  return 1;
}

// Push a batch of ops. Returns the same codes as syncPoll; on 409 the
// fresh state is parsed into the cache first (the caller just retries).
int syncPush(const String& opsJson) {
  if (WiFi.status() != WL_CONNECTED) return -1;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(7000);
  http.setTimeout(12000);
  if (!http.begin(client, String(SYNC_BASE) + "/api/sync")) return -1;
  if (strlen(SYNC_KEY)) http.addHeader("X-Api-Key", SYNC_KEY);
  http.addHeader("Content-Type", "application/json");
  String body = "{\"dev\":\"" + String(DEVICE_NAME) + "\",\"baseRev\":" + String(rev) +
                ",\"ops\":[" + opsJson + "]}";
  int code = http.POST(body);
  if (code == 409) {                       // conflict: adopt the fresh state, retry once
    String resp = http.getString();
    http.end();
    parseState(resp);                      // rev is now the server's current rev
    // rebuild the body with the NEW baseRev (re-sending the old one would 409 forever)
    body = "{\"dev\":\"" + String(DEVICE_NAME) + "\",\"baseRev\":" + String(rev) +
           ",\"ops\":[" + opsJson + "]}";
    if (!http.begin(client, String(SYNC_BASE) + "/api/sync")) return -1;
    if (strlen(SYNC_KEY)) http.addHeader("X-Api-Key", SYNC_KEY);
    http.addHeader("Content-Type", "application/json");
    code = http.POST(body);
  }
  if (code != 200) { Serial.printf("[SYNC] POST %d\n", code); http.end(); return -1; }
  String resp = http.getString();
  http.end();
  if (!parseState(resp)) return -1;
  return 1;
}

// ------------------------------------------------------------
//  JSON <-> cache
// ------------------------------------------------------------

bool parseState(const String& body) {
  JsonDocument doc;                        // ArduinoJson 7
  if (deserializeJson(doc, body)) return false;
  if (!doc["ok"].is<bool>() || !doc["ok"].as<bool>()) {
    // 409 bodies carry ok:false BUT still hold the fresh state - parse it
    if (!doc["rev"].is<uint32_t>()) return false;
  }
  rev = doc["rev"] | 0u;

  nAlarms = 0;
  for (JsonObject a : doc["alarms"].as<JsonArray>()) {
    if (nAlarms >= MAX_ENTRIES) break;
    AlarmEntry& e = alarms[nAlarms++];
    strlcpy(e.id, a["id"] | "", sizeof(e.id));
    e.enabled   = a["enabled"] | false;
    e.isReminder = strcmp(a["type"] | "alarm", "reminder") == 0;
    const char* kind = a["kind"] | "once";
    e.kind = !strcmp(kind, "daily") ? 1 : !strcmp(kind, "weekly") ? 2
           : !strcmp(kind, "countdown") ? 3 : 0;
    const char* t = a["time"] | "00:00";
    e.hour = atoi(String(t).substring(0, 2).c_str());
    e.minute = atoi(String(t).substring(3, 5).c_str());
    const char* d = a["date"] | "";
    if (strlen(d) >= 10) {
      e.year = atoi(String(d).substring(0, 4).c_str());
      e.month = atoi(String(d).substring(5, 7).c_str());
      e.day = atoi(String(d).substring(8, 10).c_str());
    }
    e.weekdays = (uint8_t)(a["weekdays"] | 0);
    e.melody = (uint8_t)(a["melody"] | 0);
    strlcpy(e.label, a["label"] | "", sizeof(e.label));
  }

  nTasks = 0;
  for (JsonObject t : doc["tasks"].as<JsonArray>()) {
    if (nTasks >= MAX_ENTRIES) break;
    TaskEntry& e = tasks[nTasks++];
    strlcpy(e.id, t["id"] | "", sizeof(e.id));
    strlcpy(e.title, t["title"] | "", sizeof(e.title));
    const char* d = t["date"] | "";
    if (strlen(d) >= 10) {
      e.year = atoi(String(d).substring(0, 4).c_str());
      e.month = atoi(String(d).substring(5, 7).c_str());
      e.day = atoi(String(d).substring(8, 10).c_str());
    }
    const char* en = t["end"] | "";
    e.hasEnd = strlen(en) >= 10;
    if (e.hasEnd) {
      e.endYear = atoi(String(en).substring(0, 4).c_str());
      e.endMonth = atoi(String(en).substring(5, 7).c_str());
      e.endDay = atoi(String(en).substring(8, 10).c_str());
    }
    const char* tm = t["time"] | "";
    e.hasTime = strlen(tm) >= 4;
    if (e.hasTime) {
      e.hour = atoi(String(tm).substring(0, 2).c_str());
      e.minute = atoi(String(tm).substring(3, 5).c_str());
    }
    e.done = t["done"] | false;
  }
  Serial.printf("[SYNC] rev %u: %d alarms, %d tasks\n", rev, nAlarms, nTasks);
  return true;
}

// ------------------------------------------------------------
//  local alarm engine (NTP clock - rings without WiFi)
// ------------------------------------------------------------

void timeBegin() {
  configTzTime("IST-5:30", "time.nplindia.in", "time.nic.in", "pool.ntp.org");
}

// call once per minute; returns the index of a due alarm, -1 none
int alarmsCheck() {
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  for (int i = 0; i < nAlarms; i++) {
    const AlarmEntry& a = alarms[i];
    if (!a.enabled) continue;
    if (a.hour != tmv.tm_hour || a.minute != tmv.tm_min) continue;
    switch (a.kind) {
      case 0: case 3:   // once / countdown: date must match too
        if (a.year == tmv.tm_year + 1900 && a.month == tmv.tm_mon + 1 && a.day == tmv.tm_mday)
          return i;
        break;
      case 1: return i;                                  // daily
      case 2: {                                          // weekly
        int bit = (tmv.tm_wday == 0) ? 6 : tmv.tm_wday - 1;   // Mon-first
        if (a.weekdays & (1 << bit)) return i;
        break;
      }
    }
  }
  return -1;
}

// ring it however the Ventuno Q rings (buzzer, screen, ...)
void fireAlarm(const AlarmEntry& a) {
  Serial.printf("[ALARM] %02u:%02u %s (%s)\n", a.hour, a.minute, a.label,
                a.isReminder ? "reminder" : "alarm");
  // >>> your ring code here <<<
}

// dismiss a one-shot from the Ventuno Q (one op, one write):
// re-send the alarm with enabled=false so the DeskBuddy sees it too
void dismissAlarm(const AlarmEntry& a) {
  String op = String("{\"op\":\"alarm.put\",\"id\":\"") + a.id + "\",\"alarm\":{" +
    "\"id\":\"" + a.id + "\",\"enabled\":false" +
    ",\"type\":\"" + (a.isReminder ? "reminder" : "alarm") + "\"" +
    ",\"kind\":\"" + String(a.kind == 1 ? "daily" : a.kind == 2 ? "weekly" : a.kind == 3 ? "countdown" : "once") + "\"" +
    ",\"time\":\"" + String(a.hour / 10) + String(a.hour % 10) + ":" + String(a.minute / 10) + String(a.minute % 10) + "\"" +
    ",\"date\":\"" + String(a.year) + "-" + String(a.month / 10) + String(a.month % 10) + "-" + String(a.day / 10) + String(a.day % 10) + "\"" +
    ",\"weekdays\":" + String(a.weekdays) +
    ",\"melody\":" + String(a.melody) +
    ",\"label\":\"" + a.label + "\"}}";
  syncPush(op);
}

// ------------------------------------------------------------
//  setup / loop
// ------------------------------------------------------------

uint32_t lastPoll = 0;
int lastMin = -1;

void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  timeBegin();
  Serial.println("[BOOT] Ventuno Q planner client");
}

void loop() {
  // poll every 30 s (1 read when something changed, 0 when not)
  if (WiFi.status() == WL_CONNECTED && millis() - lastPoll > 30000UL) {
    lastPoll = millis();
    syncPoll();
  }

  // once a minute, ring whatever is due - locally, from the cache
  time_t now = time(nullptr);
  struct tm tmv;
  localtime_r(&now, &tmv);
  if (tmv.tm_min != lastMin) {
    lastMin = tmv.tm_min;
    int i = alarmsCheck();
    if (i >= 0) {
      fireAlarm(alarms[i]);
      // one-shot alarms switch themselves off after ringing:
      if (alarms[i].kind == 0 || alarms[i].kind == 3) dismissAlarm(alarms[i]);
    }
  }
  delay(50);
}
