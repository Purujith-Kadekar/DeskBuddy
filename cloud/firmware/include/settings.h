#pragma once
#include <Arduino.h>

// ============================================================
//  settings.h - persisted user settings (NVS namespace "buddy")
// ============================================================

struct Settings {
  uint8_t volume;        // 0..10
  uint8_t brightness;    // 40..255
  bool    autoFace;      // autonomous expression cycling
  bool    cuckoo;        // hourly cuckoo chime (07:00-23:00)
  bool    hour12;        // 12-hour clock with AM/PM (default) or 24-hour
  bool    useF;          // weather in Fahrenheit
  uint8_t widgets;       // bit mask of enabled widgets (see WG_* in app.h)
  float   wxLat, wxLon;  // weather location (set from the web app)
  char    wxName[28];    // weather location name
  uint8_t pack;          // eye style  (EyePack)
  uint8_t mood;          // personality (Mood)
  char    name[17];      // buddy name shown on web
  char    tz[48];        // POSIX timezone string
  bool    dnd;           // do-not-disturb window
  uint16_t dndStart;     // minutes from midnight (e.g. 20:00 = 1200)
  uint16_t dndEnd;       // minutes from midnight (e.g. 06:00 = 360)
};

extern Settings settings;

void settingsBegin();    // load with defaults
void settingsSave();
bool dndActiveNow();     // true inside the DND window (needs a synced clock)
