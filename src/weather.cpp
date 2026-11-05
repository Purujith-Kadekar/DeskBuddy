#include "weather.h"
#include "config.h"
#include "settings.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static Weather wx = {};
static volatile bool refreshFlag = false;
static uint32_t lastTry = 0, lastOk = 0;
static char lastLocKey[40] = "";

bool weatherHasLocation() { return settings.wxName[0] != 0; }
Weather weatherGet() { return wx; }
void weatherRefresh() { refreshFlag = true; }

float weatherTemp(float c) { return settings.useF ? c * 9.0f / 5.0f + 32.0f : c; }

const char* weatherCondition(int c) {
  switch (c) {
    case 0:  return "Clear";
    case 1:  return "Mostly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Foggy";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Icy drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Icy rain";
    case 71: case 73: case 75: case 77: return "Snow";
    case 80: case 81: return "Showers";
    case 82: return "Downpour";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Hail storm";
    default: return "Unknown";
  }
}

WeatherIcon weatherIcon(int c, bool day) {
  if (c == 0 || c == 1) return day ? WI_SUN : WI_MOON;
  if (c == 2) return day ? WI_PARTLY : WI_PARTLY_NIGHT;
  if (c == 3) return WI_CLOUD;
  if (c == 45 || c == 48) return WI_FOG;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WI_SNOW;
  if (c >= 95) return WI_STORM;
  return WI_RAIN;                                   // drizzle, rain, showers
}

// number that follows  "key":   (also works for  "key":[12.3, ...])
static bool numAfter(const String& s, int from, const char* key, float& out) {
  String k = String("\"") + key + "\":";
  int i = s.indexOf(k, from);
  if (i < 0) return false;
  i += k.length();
  while (i < (int)s.length() && (s[i] == ' ' || s[i] == '[')) i++;
  out = s.substring(i, i + 12).toFloat();
  return true;
}

static bool fetchWeather() {
  char url[300];
  snprintf(url, sizeof(url),
    "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
    "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m"
    "&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max&timezone=auto&forecast_days=1",
    (double)settings.wxLat, (double)settings.wxLon);

  WiFiClientSecure client;
  client.setInsecure();                              // public weather data: no certificate pinning needed
  HTTPClient http;
  http.useHTTP10(true);
  http.setConnectTimeout(7000);
  http.setTimeout(9000);
  if (!http.begin(client, String(url))) return false;
  int code = http.GET();
  if (code != 200) {
    Serial.printf("[WEATHER] HTTP %d\n", code);
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  int c = body.indexOf("\"current\":{");
  int d = body.indexOf("\"daily\":{");
  float t, f, h, w, wc, day, hi = 0, lo = 0, rn = 0;
  if (c < 0 || !numAfter(body, c, "temperature_2m", t)) return false;
  numAfter(body, c, "apparent_temperature", f);
  numAfter(body, c, "relative_humidity_2m", h);
  numAfter(body, c, "weather_code", wc);
  numAfter(body, c, "is_day", day);
  numAfter(body, c, "wind_speed_10m", w);
  if (d >= 0) {
    numAfter(body, d, "temperature_2m_max", hi);
    numAfter(body, d, "temperature_2m_min", lo);
    numAfter(body, d, "precipitation_probability_max", rn);
  }
  Weather n;
  n.valid = true; n.temp = t; n.feels = f; n.humidity = (int)h; n.code = (int)wc;
  n.isDay = day > 0.5f; n.wind = w; n.hi = hi; n.lo = lo; n.rain = (int)rn; n.fetchedMs = millis();
  wx = n;
  Serial.printf("[WEATHER] %s: %.1fC, %s\n", settings.wxName, (double)t, weatherCondition((int)wc));
  return true;
}

static void weatherTask(void*) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(1000));
    if (WiFi.status() != WL_CONNECTED || !weatherHasLocation()) continue;

    char key[40];                                    // location changed -> old data is wrong
    snprintf(key, sizeof(key), "%.4f,%.4f", (double)settings.wxLat, (double)settings.wxLon);
    if (strcmp(key, lastLocKey) != 0) {
      strncpy(lastLocKey, key, sizeof(lastLocKey) - 1);
      wx.valid = false;
      lastOk = 0; lastTry = 0;
      refreshFlag = true;
    }
    uint32_t now = millis();
    bool due = refreshFlag ||
               (lastOk == 0 && (lastTry == 0 || now - lastTry >= 20000UL)) ||
               (lastOk != 0 && now - lastOk >= 30UL * 60UL * 1000UL && now - lastTry >= 60000UL);
    if (!due) continue;
    refreshFlag = false;
    lastTry = now ? now : 1;
    if (fetchWeather()) lastOk = millis() ? millis() : 1;
  }
}

void weatherBegin() {
  xTaskCreate(weatherTask, "weather", 12288, nullptr, 1, nullptr);
}
