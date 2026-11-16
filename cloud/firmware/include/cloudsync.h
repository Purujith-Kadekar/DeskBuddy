#pragma once
#include <Arduino.h>

// ============================================================
//  cloudsync.h - the Vercel sync client (DeskBuddy v3.20)
//
//  Vercel is the single source of truth for alarms, reminders
//  and tasks. The buddy keeps its own copy in NVS so alarms
//  still ring on time even with WiFi down (local NTP clock),
//  and pushes any change made on the device (web UI, dismiss,
//  task check-off) up as small "ops" - one POST per batch.
//
//  Protocol (see ARCHITECTURE.md in the webapp repo):
//    GET  {BASE}/api/sync?dev=<name>&rev=N
//         -> 200 {rev, tasks[], alarms[]} | 304 (nothing new)
//    POST {BASE}/api/sync   {dev, baseRev, ops[]}
//         -> 200 {rev, tasks[], alarms[]}   (ops applied, whole doc)
//         -> 409 {rev, tasks[], alarms[]}   (stale baseRev: re-apply)
//  Firestore cost: 1 read per poll (0 when 304), a change is
//  1 read + 1 write - always the whole document, never partial.
// ============================================================

// leave empty to disable the cloud entirely (offline buddy as before).
// Put your deployment URL in include/wifi_credentials.h instead:
//   #define CLOUD_BASE_URL   "https://your-app.vercel.app"
//   #define CLOUD_API_KEY    "a-long-random-string"     (optional)
#ifndef CLOUD_BASE_URL
#define CLOUD_BASE_URL     ""
#endif
#ifndef CLOUD_API_KEY
#define CLOUD_API_KEY      ""
#endif
// poll cadence
#define CLOUD_POLL_S       30          // normal poll (2 reads/min)
#define CLOUD_BACKOFF_S    120         // gap after a failed attempt
#define CLOUD_PUSH_S       20          // wait before pushing ops (batching window)

enum CloudState : uint8_t {
  CL_OFF = 0,      // no CLOUD_BASE_URL configured
  CL_WAIT,         // configured, no WiFi / not tried yet
  CL_SYNC,         // in sync with the cloud
  CL_SYNCING,      // a transfer is running right now
  CL_PENDING,      // local ops waiting to go up
  CL_ERROR         // last attempt failed (retrying with backoff)
};

struct Alarm;   // alarms.h
struct Task;    // tasks.h

void      cloudBegin();               // start the background task (from setup)
void      cloudTick();                // main-loop hook: apply staged cloud state
CloudState cloudState();
const char* cloudStateName();         // "sync" / "pending" / "error" / ...
uint32_t   cloudLastOkMs();           // millis() of the last successful exchange
uint32_t   cloudRev();                // revision we are standing on
int        cloudQueuedOps();          // ops waiting to be pushed
void      cloudRequestSync();         // poll again at the next tick (after edits)

// queue ops for changes made on the device (one POST carries them all)
void cloudQueueAlarmPut(const Alarm& a);
void cloudQueueTaskPut(const Task& t);
void cloudQueueAlarmDel(const char* id);
void cloudQueueTaskDel(const char* id);

// declared in app.cpp: true while an alarm/reminder is actively ringing
bool appRingingExternally();
