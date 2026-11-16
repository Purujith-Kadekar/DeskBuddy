// ============================================================
//  main.cpp - Desk Buddy boot & main loop
//  ESP32-C3 + SH1106 OLED + TTP223 touch + tilt + buzzer
//
//  WiFi: station mode only (no hotspot). Put your router name and
//  password in include/wifi_credentials.h (git-ignored).
//  The join is non-blocking, so the buddy wakes up and animates
//  immediately; the web page + clock sync start by themselves the
//  moment WiFi connects.
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "config.h"
#include "display.h"
#include "faces.h"
#include "audio.h"
#include "alarms.h"
#include "tasks.h"
#include "clocksync.h"
#include "sensors.h"
#include "settings.h"
#include "pomodoro.h"
#include "app.h"
#include "webapi.h"
#include "weather.h"
#include "ota.h"

static uint32_t frameLast = 0;

// Fail the BUILD (not the upload, minutes later) if the WiFi details were never filled in.
constexpr bool sameStr(const char* a, const char* b) { return *a == *b && (*a == 0 || sameStr(a + 1, b + 1)); }
static_assert(!sameStr(WIFI_SSID, "YourWiFiName"),
              "Set WIFI_SSID and WIFI_PASS in include/wifi_credentials.h before building");

// ---------------- WiFi (the same recipe that connected on this board) ----------------
static bool     haveWifi = false;
static bool     wasUp = false;
static uint32_t lastTry = 0;

static void wifiJoin() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                  // (v3.16) radio never naps: modem sleep slows transfers to a crawl
  WiFi.setAutoReconnect(true);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);   // (v3.19) 20 MHz channel: far more robust on a crowded 2.4 GHz band
  WiFi.setTxPower(WIFI_TX_POWER);        // lower TX power: many ESP32-C3 boards only join like this
  WiFi.begin(WIFI_SSID, WIFI_PASS);
}

static void wifiBegin() {
  haveWifi = strlen(WIFI_SSID) > 0;
  if (!haveWifi) {
    Serial.println("[WIFI] no network set - put WIFI_SSID / WIFI_PASS in include/wifi_credentials.h");
    return;
  }
  Serial.printf("[WIFI] joining '%s' ...\n", WIFI_SSID);
  wifiJoin();
  lastTry = millis();
}

static void wifiTick(uint32_t now) {
  if (!haveWifi) return;
  bool up = WiFi.status() == WL_CONNECTED;

  if (up && !wasUp) {
    Serial.printf("[WIFI] connected. Web app: http://%s  (or http://%s.local)\n",
                  WiFi.localIP().toString().c_str(), MDNS_HOST);
    webStart();                          // start the web server + mDNS
    clockOnWifi();                       // start NTP (NPL / NIC India)
    otaStart();                          // wireless updates: espota + http://...:8080
  }
  if (!up && wasUp) Serial.println("[WIFI] connection lost, re-joining");
  wasUp = up;

  // not connected: every 15 s restart the radio and try again
  if (!up && now - lastTry > WIFI_RETRY_MS) {
    lastTry = now;
    Serial.printf("[WIFI] retry (status %d)\n", (int)WiFi.status());
    WiFi.disconnect(true);
    delay(100);
    wifiJoin();
  }
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n[BOOT] Desk Buddy v" BUDDY_VERSION);

  displayBegin();
  settingsBegin();
  displaySetBrightness(settings.brightness);

  alarmsBegin();
  tasksBegin();                          // (was never called before v3.9: tasks vanished on every reboot)
  audioBegin(settings.volume);
  sensorsBegin();
  pomoBegin();
  appBegin();                            // the buddy wakes up: eyes open

  clockBegin(settings.tz);               // timezone is IST from the very first second
  webBegin();                            // register routes (server itself starts once WiFi is up)
  wifiBegin();
  weatherBegin();                        // background weather fetch (waits for WiFi + a chosen location)

  Serial.println("[BOOT] ready");
}

void loop() {
  otaTick();                              // OTA first: streaming firmware needs frequent calls
  if (ota_busy) { delay(2); return; }     // (v3.16) update in progress: pause the buddy, but keep
                                           // otaTick running ~500x/s so the upload is not throttled

  uint32_t now = millis();
  float dt = (frameLast == 0) ? 0.033f : (now - frameLast) / 1000.0f;

  // touch / tilt are sampled by their own task (sensors.cpp), not here
  clockTick();
  webTick();
  wifiTick(now);

  if (now - frameLast >= FRAME_MS || frameLast == 0) {
    if (dt > 0.25f) dt = 0.25f;
    appTick(dt);
    frameLast = now;
  }
  delay(2);
}
