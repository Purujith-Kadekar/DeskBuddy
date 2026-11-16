#pragma once
// ============================================================
//  wifi_credentials.example.h - copy this file to wifi_credentials.h
//  and fill it in. wifi_credentials.h is git-ignored; this example is not.
//  The real wifi_credentials.h holds YOUR private settings and is in
//  .gitignore, so it is never committed.
//  (The name matters: it must not be "wifi.h" - on Windows that
//   name collides with the ESP32 library header <WiFi.h>.)
// ============================================================

// your network (2.4 GHz only)
#define WIFI_SSID           "YourWiFiName"
#define WIFI_PASS           "YourWiFiPassword"

// password for flashing new firmware over WiFi (network upload and the
// browser update page). Change it to anything you like; letters and
// digits are safest (the update .bat script is picky about symbols).
#define OTA_PASSWORD        "ChangeMe"

// ---------------- cloud sync (v3.20) ----------------
// Where the planner lives. Uncomment and set after you deploy the webapp:
//
//   #define CLOUD_BASE_URL   "https://your-app.vercel.app"
//   #define CLOUD_API_KEY    "a-long-random-string"
//
// CLOUD_API_KEY must equal the SYNC_API_KEY env var in Vercel (omit the
// define if you did not set one). With no CLOUD_BASE_URL the buddy runs
// exactly like before - fully offline, planner only in NVS.

