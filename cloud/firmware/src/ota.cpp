// ============================================================
//  ota.cpp - wireless firmware updates (no USB cable needed)
//
//  Two doors in, both locked with OTA_PASSWORD (wifi_credentials.h):
//    1. Network upload (PlatformIO tools/ota_update.bat, Arduino IDE):
//       espota talks UDP 3232 to the chip.
//    2. Any browser: http://deskbuddy.local:8080 (login deskbuddy /
//       OTA_PASSWORD) - the page never fights the web app, it lives on
//       its own port.
//
//  A failed or interrupted upload never harms the running firmware: the new
//  image goes to the spare app slot (ota_0 / ota_1) and is only activated
//  after it arrived complete. If the power dies mid-upload the old firmware
//  boots as if nothing happened.
//
//  Speed (v3.16): while an upload runs the radio boosts to WIFI_TX_POWER_OTA
//  (every lost TCP ACK throttles the transfer) and drops back afterwards.
//  Joining/normal operation keeps the proven low WIFI_TX_POWER, which is what
//  some ESP32-C3 boards need to connect at all.
// ============================================================
#include "config.h"
#include "ota.h"

#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <ArduinoOTA.h>

static WebServer updateServer(8080);   // own port: never fights the app on :80
static bool started = false;

volatile bool ota_busy = false;
static bool     webUpload = false;     // a browser/curl upload is in progress
static uint32_t lastData = 0;          // millis() of the last chunk received
static uint32_t gotBytes = 0;
static uint32_t nextLog = 0;
static uint32_t startMs = 0;
static uint8_t  lastPct = 255;

static void radioBoost(bool on) {
  WiFi.setTxPower(on ? WIFI_TX_POWER_OTA : WIFI_TX_POWER);
}

static const char UPDATE_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Desk Buddy update</title>
<style>body{font-family:system-ui,sans-serif;background:#12192b;color:#e8eef8;
text-align:center;padding-top:12vh}h2{font-weight:600}code{color:#7cc4ff}
input{margin:8px;padding:8px}</style></head><body>
<h2>Desk Buddy &mdash; firmware update</h2>
<p>Pick the .bin built by PlatformIO:<br>
<code>.pio/build/esp32c3/firmware.bin</code></p>
<form method="POST" action="/update" enctype="multipart/form-data">
<input type="file" name="firmware" accept=".bin" required><br>
<input type="submit" value="Upload and flash">
</form></body></html>)HTML";

// one password for both doors: OTA_PASSWORD from wifi_credentials.h
static bool authOk() {
  if (!updateServer.authenticate("deskbuddy", OTA_PASSWORD)) {
    updateServer.requestAuthentication();
    return false;
  }
  return true;
}

void otaStart() {
  if (started) return;
  started = true;

  // ---- door 1: network upload (espota / Arduino IDE) ----
  ArduinoOTA.setHostname(MDNS_HOST);       // same name as the web app
  ArduinoOTA.setPassword(OTA_PASSWORD);
  ArduinoOTA.onStart([]() {
    lastPct = 255;
    ota_busy = true;
    radioBoost(true);                      // full TX power for the upload
    Serial.println("[OTA] upload started");
  });
  ArduinoOTA.onEnd([]() {                  // the library reboots by itself
    radioBoost(false);
    Serial.println("[OTA] done - rebooting");
  });
  ArduinoOTA.onProgress([](unsigned int d, unsigned int t) { 
    // (v3.19) print only every 10 %: a Serial.print per chunk BLOCKS on ESP32-C3 USB-CDC when no
    // serial monitor is open, which alone can throttle an upload to a crawl
    uint8_t p = t ? (uint8_t)(d * 100ULL / t) : 0;
    if (p != lastPct && p % 10 == 0) { lastPct = p; Serial.printf("[OTA] %u%%  RSSI %d dBm\n", p, (int)WiFi.RSSI()); }
  });
  ArduinoOTA.onError([](ota_error_t e) {
    radioBoost(false);
    ota_busy = false;
    Serial.printf("[OTA] error %u (auth or transfer failed)\n", (unsigned)e);
  });
  ArduinoOTA.begin();

  // ---- door 2: browser page on :8080 ----
  updateServer.on("/update", HTTP_GET, []() {
    if (!authOk()) return;
    updateServer.send_P(200, "text/html", UPDATE_PAGE);
  });

  updateServer.on("/update", HTTP_POST,
    []() {                                   // runs once, after the body streamed in
      radioBoost(false);
      if (!authOk()) return;
      if (Update.hasError()) {
        ota_busy = false;
        updateServer.send(500, "text/plain", "update FAILED - nothing changed, try again");
      } else {
        updateServer.send(200, "text/plain", "OK - rebooting now");
        delay(700);                          // let the reply leave the chip
        ESP.restart();
      }
    },
    []() {                                   // runs repeatedly while the .bin streams in
      HTTPUpload& up = updateServer.upload();
      if (up.status == UPLOAD_FILE_START) {
        // headers are parsed before the body: check them before writing anything
        if (!updateServer.authenticate("deskbuddy", OTA_PASSWORD)) return;
        ota_busy = true;
        webUpload = true;
        lastData = startMs = millis(); gotBytes = 0; nextLog = 131072;
        radioBoost(true);
        Serial.printf("[OTA] web upload: %s  (RSSI %d dBm, heap %u)\n", up.filename.c_str(), (int)WiFi.RSSI(), (unsigned)ESP.getFreeHeap());
        Update.begin(UPDATE_SIZE_UNKNOWN);
      } else if (up.status == UPLOAD_FILE_WRITE && ota_busy) {
        size_t w = Update.write(up.buf, up.currentSize);
        if (w != up.currentSize) { Serial.printf("[OTA] write failed (%u of %u)\n", (unsigned)w, (unsigned)up.currentSize); Update.printError(Serial); }
        lastData = millis();
        gotBytes += up.currentSize;
        if (gotBytes >= nextLog) {
          uint32_t ms = millis() - startMs; if (!ms) ms = 1;
          Serial.printf("[OTA] web: %u KB  %u KB/s  RSSI %d dBm\n", (unsigned)(gotBytes / 1024), (unsigned)(gotBytes / ms), (int)WiFi.RSSI());
          nextLog = gotBytes + 131072;
        }
      } else if (up.status == UPLOAD_FILE_ABORTED && ota_busy) {
        // connection dropped mid-upload: UNFREEZE the buddy (it used to stay paused forever)
        Serial.println("[OTA] upload aborted (connection lost) - resuming normal operation");
        Update.abort();
        radioBoost(false);
        webUpload = false;
        ota_busy = false;
      } else if (up.status == UPLOAD_FILE_END && ota_busy) {
        if (Update.end(true)) Serial.printf("[OTA] wrote %u bytes\n", (unsigned)up.totalSize);
        else { ota_busy = false; Update.printError(Serial); }
      }
    });

  updateServer.begin();

  Serial.printf("[OTA] ready | network: %s.local | browser: http://%s:8080\n",
                MDNS_HOST, WiFi.localIP().toString().c_str());
}

void otaTick() {
  if (!started) return;
  ArduinoOTA.handle();
  updateServer.handleClient();
  // safety net: a web upload that went silent for 20 s must not freeze the buddy for good
  if (webUpload && ota_busy && millis() - lastData > 20000UL) {
    Serial.println("[OTA] upload stalled 20 s - giving up, resuming normal operation");
    Update.abort();
    radioBoost(false);
    webUpload = false;
    ota_busy = false;
  }
}
