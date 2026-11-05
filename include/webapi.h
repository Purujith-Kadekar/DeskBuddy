#pragma once
// ============================================================
//  webapi.h - WiFi web server (REST API + embedded web UI)
//
//  !! Deliberately NOT named "webserver.h" !!
//  On Windows the filesystem is case-insensitive, so a project
//  header named webserver.h shadows the ESP32 framework's
//  <WebServer.h> (PlatformIO puts include/ first in the search
//  path) and the build fails with "'WebServer' does not name
//  a type". Keep this filename different from any framework
//  header.
// ============================================================
void webBegin();       // register the routes (safe before WiFi is up)
void webStart();       // start the HTTP server + mDNS - call when WiFi has connected
void webTick();        // handleClient (does nothing until webStart())
