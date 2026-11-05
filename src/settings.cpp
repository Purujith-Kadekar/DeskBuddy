#include "settings.h"
#include <Preferences.h>
#include "config.h"
#include "clocksync.h"

Settings settings;

void settingsBegin() {
  // defaults
  settings.volume = 6;
  settings.brightness = 180;
  settings.autoFace = true;
  settings.cuckoo = true;
  settings.hour12 = true;
  settings.useF = false;
  settings.widgets = 0x1F;
  settings.wxLat = 0; settings.wxLon = 0; settings.wxName[0] = 0;
  settings.pack = 0;
  settings.mood = 0;
  settings.dnd = false;                     // off by default: 8 PM - 6 AM
  settings.dndStart = 20 * 60;
  settings.dndEnd = 6 * 60;
  strncpy(settings.name, BUDDY_NAME_DEF, sizeof(settings.name) - 1);
  settings.name[sizeof(settings.name) - 1] = 0;
  strncpy(settings.tz, TZ_DEFAULT, sizeof(settings.tz) - 1);
  settings.tz[sizeof(settings.tz) - 1] = 0;

  Preferences p;
  if (!p.begin("buddy", true)) return;
  settings.volume = p.getUChar("volume", settings.volume);
  settings.brightness = p.getUChar("bright", settings.brightness);
  settings.autoFace = p.getBool("autoface", settings.autoFace);
  settings.cuckoo = p.getBool("cuckoo", settings.cuckoo);
  settings.hour12 = p.getBool("hour12", settings.hour12);
  settings.useF = p.getBool("useF", settings.useF);
  settings.widgets = p.getUChar("widgets", settings.widgets);
  settings.wxLat = p.getFloat("wxLat", settings.wxLat);
  settings.wxLon = p.getFloat("wxLon", settings.wxLon);
  {
    String wn = p.getString("wxName", "");
    strncpy(settings.wxName, wn.c_str(), sizeof(settings.wxName) - 1);
    settings.wxName[sizeof(settings.wxName) - 1] = 0;
  }
  settings.pack = p.getUChar("pack", settings.pack);
  settings.mood = p.getUChar("mood", settings.mood);
  String n = p.getString("name", settings.name);
  strncpy(settings.name, n.c_str(), sizeof(settings.name) - 1);
  settings.name[sizeof(settings.name) - 1] = 0;
  String tz = p.getString("tz", settings.tz);
  strncpy(settings.tz, tz.c_str(), sizeof(settings.tz) - 1);
  settings.tz[sizeof(settings.tz) - 1] = 0;
  settings.dnd = p.getBool("dnd", settings.dnd);
  settings.dndStart = p.getUShort("dndS", settings.dndStart);
  settings.dndEnd = p.getUShort("dndE", settings.dndEnd);
  uint8_t tzv = p.getUChar("tzv", 0);
  uint8_t wv = p.getUChar("wv", 0);
  p.end();

  // one-time migration: older builds saved a European default timezone -> force India (IST)
  if (tzv < 3) {
    strncpy(settings.tz, TZ_DEFAULT, sizeof(settings.tz) - 1);
    settings.tz[sizeof(settings.tz) - 1] = 0;
    Preferences w;
    if (w.begin("buddy", false)) {
      w.putString("tz", String(settings.tz));
      w.putUChar("tzv", 3);
      w.end();
    }
  }

  // one-time migration: v3.6 added the Tasks widget as bit 3 and moved
  // "Up next" to bit 4. Re-map a saved 4-widget mask once and enable Tasks.
  if (wv < 1) {
    settings.widgets = (uint8_t)((settings.widgets & 0x07) | ((settings.widgets & 0x08) << 1) | 0x08);
    Preferences w;
    if (w.begin("buddy", false)) {
      w.putUChar("wv", 1);
      w.putUChar("widgets", settings.widgets);
      w.end();
    }
  }

  if (settings.volume > 10) settings.volume = 10;
  if ((settings.widgets & 0x1F) == 0) settings.widgets = 0x1F;       // at least one widget
  if (settings.pack >= 6) settings.pack = 0;
  if (settings.mood >= 7) settings.mood = 0;
  if (settings.brightness < 40) settings.brightness = 40;
  if (settings.dndStart > 1439) settings.dndStart = 20 * 60;
  if (settings.dndEnd > 1439) settings.dndEnd = 6 * 60;
}

void settingsSave() {
  Preferences p;
  p.begin("buddy", false);
  p.putUChar("volume", settings.volume);
  p.putUChar("bright", settings.brightness);
  p.putBool("autoface", settings.autoFace);
  p.putBool("cuckoo", settings.cuckoo);
  p.putBool("hour12", settings.hour12);
  p.putBool("useF", settings.useF);
  p.putUChar("widgets", settings.widgets);
  p.putFloat("wxLat", settings.wxLat);
  p.putFloat("wxLon", settings.wxLon);
  p.putString("wxName", String(settings.wxName));
  p.putUChar("pack", settings.pack);
  p.putUChar("mood", settings.mood);
  p.putString("name", String(settings.name));
  p.putString("tz", String(settings.tz));
  p.putBool("dnd", settings.dnd);
  p.putUShort("dndS", settings.dndStart);
  p.putUShort("dndE", settings.dndEnd);
  p.end();
}

// inside the do-not-disturb window? handles windows that wrap midnight
// (20:00 -> 06:00) and same-day ones (09:00 -> 17:00); start == end = always
bool dndActiveNow() {
  if (!settings.dnd || !clockOk()) return false;
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  int m = tmv.tm_hour * 60 + tmv.tm_min;
  int a = settings.dndStart, b = settings.dndEnd;
  if (a == b) return true;
  return a < b ? (m >= a && m < b) : (m >= a || m < b);
}
