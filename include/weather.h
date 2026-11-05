#pragma once
#include <Arduino.h>

// ============================================================
//  weather.h - current weather from Open-Meteo (free, no API key)
//
//  The location (name + latitude/longitude) is chosen in the web app
//  and stored in settings. A background task fetches every 30 minutes
//  so the animation / web server never stall on the TLS request.
// ============================================================

enum WeatherIcon : uint8_t { WI_SUN = 0, WI_MOON, WI_PARTLY, WI_PARTLY_NIGHT, WI_CLOUD, WI_RAIN, WI_STORM, WI_SNOW, WI_FOG };

struct Weather {
  bool     valid;
  float    temp, feels, hi, lo, wind;     // always Celsius / km/h here
  int      code, humidity, rain;          // WMO code, %, max rain probability %
  bool     isDay;
  uint32_t fetchedMs;
};

void           weatherBegin();            // start the background task
void           weatherRefresh();          // fetch as soon as possible (location changed, user asked)
Weather        weatherGet();              // copy of the latest data
bool           weatherHasLocation();
const char*    weatherCondition(int code);     // "Partly cloudy"
WeatherIcon    weatherIcon(int code, bool isDay);
float          weatherTemp(float celsius);     // converts to the chosen unit (C or F)
