// ============================================================
//  webapi.h - the WiFi web server (src/webapi.cpp) and the
//  pieces of it the USB command channel shares with it.
//
//  (The file name matters: on Windows a project header named
//  webserver.h case-insensitively shadows the framework <WebServer.h>
//  and breaks the build - see src/webapi.cpp.)
//
//  v3.21: the Ventuno Q drives the buddy over the USB cable as well
//  as over the LAN.  The USB channel (src/serialcmd.cpp) must parse an
//  alarm / a task exactly like the REST API does, so the parsing lives
//  here and both callers use it.  Nothing about the REST API changes.
// ============================================================
#pragma once

#include <Arduino.h>
#include "alarms.h"
#include "tasks.h"

// ---- the web server (src/webapi.cpp) ----
void webBegin();      // register the routes (called once)
void webStart();      // announce the server on mDNS (after WiFi is up)
void webTick();       // service the server (called from the main loop)

// ---- shared parsing (src/webapi.cpp) ----
// Parse an alarm body (the same JSON the REST API accepts):
//   {"id","enabled","type","kind","time","date","weekdays","melody",
//    "label","minutes"}
// Returns false when the body is not a valid alarm.  idOut receives the
// id the body asked for (empty = create a new entry).
bool parseAlarmBody(const String& body, Alarm& a, String& idOut);

// Parse a task body:
//   {"id","title","date","end","time","done"}
// Returns false when the body is not a valid task; err explains why.
bool parseTaskBody(const String& body, Task& t, String& idOut,
                   String& err);
