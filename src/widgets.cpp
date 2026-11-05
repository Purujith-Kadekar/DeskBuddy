#include "widgets.h"
#include "faces.h"
#include "config.h"
#include "settings.h"
#include "clocksync.h"
#include "weather.h"
#include "alarms.h"
#include "tasks.h"
#include <time.h>
#include <WiFi.h>

const char* widgetName(uint8_t id) {
  static const char* n[WG_COUNT] = { "clock", "weather", "date", "tasks", "up next" };
  return id < WG_COUNT ? n[id] : "?";
}

uint8_t widgetNext(uint8_t cur, uint8_t mask) {
  mask &= WG_ALL_MASK;
  if (!mask) return WG_CLOCK;
  for (int i = 1; i <= WG_COUNT; i++) {
    uint8_t n = (uint8_t)((cur + i) % WG_COUNT);
    if (mask & (1 << n)) return n;
  }
  return cur;
}

static void centerStr(U8G2& g, int y, const char* s) {
  int x = 64 - g.getStrWidth(s) / 2;
  if (x < 0) x = 0;                                  // u8g2 cannot draw at negative x (it wraps around)
  g.drawStr(x, y, s);
}

// ---------------------------------------------------------------- weather icons
// All drawn with clip-safe primitives. (cx,cy) = centre of a ~28x26 box.
static void cloudShape(U8G2& g, int cx, int cy, uint8_t col, int grow) {
  gfxEllipse(g, cx - 6, cy + 2, 5 + grow, 5 + grow, col);
  gfxEllipse(g, cx + 2, cy - 2, 7 + grow, 7 + grow, col);
  gfxEllipse(g, cx + 8, cy + 3, 5 + grow, 5 + grow, col);
  gfxRBox(g, cx - 11 - grow, cy + 2 - grow, 24 + 2 * grow, 8 + 2 * grow, 4, col);
}
static void sunShape(U8G2& g, int cx, int cy, int r, int rays, float t) {
  gfxEllipse(g, cx, cy, r, r, 1);
  for (int k = 0; k < 8; k++) {
    float a = k * 0.7854f + t * 0.6f;
    int r0 = r + 3, r1 = r + 3 + rays + ((k & 1) ? 0 : 1);
    gfxLine(g, cx + (int)lroundf(cosf(a) * r0), cy + (int)lroundf(sinf(a) * r0),
            cx + (int)lroundf(cosf(a) * r1), cy + (int)lroundf(sinf(a) * r1), 1);
  }
}
static void drawWeatherIcon(U8G2& g, WeatherIcon ic, int cx, int cy) {
  float t = millis() / 1000.0f;
  switch (ic) {
    case WI_SUN: sunShape(g, cx, cy, 6, 3, t); break;
    case WI_MOON:
      gfxEllipse(g, cx - 1, cy, 9, 9, 1);
      gfxEllipse(g, cx + 4, cy - 3, 8, 8, 0);
      gfxPixel(g, cx + 9, cy - 8, 1); gfxPixel(g, cx + 6, cy + 4, 1);
      if (((int)(t * 2)) & 1) gfxPixel(g, cx + 10, cy + 1, 1);
      break;
    case WI_PARTLY:
    case WI_PARTLY_NIGHT:
      if (ic == WI_PARTLY) sunShape(g, cx - 5, cy - 5, 5, 2, t);
      else { gfxEllipse(g, cx - 5, cy - 5, 7, 7, 1); gfxEllipse(g, cx - 1, cy - 8, 6, 6, 0); }
      cloudShape(g, cx + 2, cy + 3, 0, 1);               // dark outline separates it from the sun
      cloudShape(g, cx + 2, cy + 3, 1, 0);
      break;
    case WI_CLOUD:
      cloudShape(g, cx, cy, 1, 0);
      break;
    case WI_RAIN:
      cloudShape(g, cx, cy - 5, 1, 0);
      for (int i = 0; i < 3; i++) {
        int y = cy + 7 + (int)fmodf(t * 14.0f + i * 5.0f, 8.0f), x = cx - 7 + i * 7;
        gfxLine(g, x, y, x - 2, y + 3, 1);
      }
      break;
    case WI_STORM: {
      cloudShape(g, cx, cy - 5, 1, 0);
      if (((int)(t * 3)) % 3 != 0) {                      // flickering bolt
        gfxLine(g, cx + 2, cy + 5, cx - 2, cy + 10, 1);
        gfxLine(g, cx - 2, cy + 10, cx + 2, cy + 10, 1);
        gfxLine(g, cx + 2, cy + 10, cx - 2, cy + 15, 1);
        gfxLine(g, cx + 3, cy + 5, cx - 1, cy + 10, 1);
      }
      break;
    }
    case WI_SNOW:
      cloudShape(g, cx, cy - 5, 1, 0);
      for (int i = 0; i < 3; i++) {
        int y = cy + 7 + (int)fmodf(t * 6.0f + i * 3.0f, 8.0f), x = cx - 7 + i * 7 + (int)(sinf(t * 2 + i) * 1.5f);
        gfxPixel(g, x, y, 1); gfxPixel(g, x + 1, y, 1); gfxPixel(g, x, y + 1, 1); gfxPixel(g, x - 1, y, 1); gfxPixel(g, x, y - 1, 1);
      }
      break;
    case WI_FOG:
      for (int i = 0; i < 4; i++) {
        int off = (int)(sinf(t * 1.2f + i * 1.7f) * 3.0f);
        int len = 18 - (i & 1) * 5;
        gfxHLine(g, cx - len / 2 + off, cy - 8 + i * 5, len, 1);
        gfxHLine(g, cx - len / 2 + off, cy - 7 + i * 5, len, 1);
      }
      break;
  }
}

// ---------------------------------------------------------------- the widgets
static void widgetClock(U8G2& g) {
  char hm[8] = "--:--", ap[4] = "", date[24] = "";
  bool colon = true;
  if (clockOk()) {
    time_t t = time(nullptr);
    struct tm tmv; localtime_r(&t, &tmv);
    clockParts(tmv, hm, sizeof(hm), ap, sizeof(ap));
    strftime(date, sizeof(date), "%a %d %b", &tmv);
    colon = (tmv.tm_sec & 1) == 0;
  }
  // "12:45" is drawn in two halves so the colon can blink without the digits moving
  char left[4] = "", right[4] = "";
  const char* c = strchr(hm, ':');
  if (c) { size_t n = (size_t)(c - hm); if (n > 3) n = 3; memcpy(left, hm, n); left[n] = 0; snprintf(right, sizeof(right), "%s", c + 1); }
  g.setFont(u8g2_font_logisoso24_tn);
  int wl = g.getStrWidth(left), wc = g.getStrWidth(":"), wr = g.getStrWidth(right);
  int apw = 0;
  if (ap[0]) { g.setFont(u8g2_font_6x10_tr); apw = g.getStrWidth(ap) + 3; g.setFont(u8g2_font_logisoso24_tn); }
  int x = 64 - (wl + wc + wr + apw) / 2;
  g.drawStr(x, 55, left);
  if (colon) g.drawStr(x + wl, 55, ":");
  g.drawStr(x + wl + wc, 55, right);
  if (ap[0]) { g.setFont(u8g2_font_6x10_tr); g.drawStr(x + wl + wc + wr + 3, 55, ap); }
  g.setFont(u8g2_font_5x7_tr);
  centerStr(g, 63, date);
}

static void widgetDate(U8G2& g) {
  if (!clockOk()) { g.setFont(u8g2_font_6x10_tr); centerStr(g, 50, "waiting for time"); return; }
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  char day[4], wd[16], my[24];
  snprintf(day, sizeof(day), "%d", tmv.tm_mday);
  strftime(wd, sizeof(wd), "%A", &tmv);
  strftime(my, sizeof(my), "%B %Y", &tmv);
  g.setFont(u8g2_font_logisoso24_tn);
  int dw = g.getStrWidth(day);
  g.drawStr(10 + (36 - dw) / 2, 57, day);
  for (int y = 33; y < 59; y++) gfxPixel(g, 52, y, 1);          // thin divider
  g.setFont(u8g2_font_6x12_tr);
  g.drawStr(58, 46, wd);
  g.setFont(u8g2_font_5x8_tr);
  g.drawStr(58, 58, my);
}

static void widgetWeather(U8G2& g) {
  Weather w = weatherGet();
  if (!weatherHasLocation()) {
    drawWeatherIcon(g, WI_CLOUD, 64, 41);
    g.setFont(u8g2_font_5x8_tr);
    centerStr(g, 59, "set location in web app");
    return;
  }
  if (!w.valid) {
    drawWeatherIcon(g, WI_CLOUD, 64, 41);
    g.setFont(u8g2_font_5x8_tr);
    centerStr(g, 59, (WiFi.status() == WL_CONNECTED) ? "loading weather..." : "no wifi");
    return;
  }
  drawWeatherIcon(g, weatherIcon(w.code, w.isDay), 20, 43);
  char num[8];
  snprintf(num, sizeof(num), "%d", (int)lroundf(weatherTemp(w.temp)));
  g.setFont(u8g2_font_logisoso24_tn);
  int nw = g.getStrWidth(num);
  int x = 40;
  g.drawStr(x, 55, num);
  gfxEllipse(g, x + nw + 4, 36, 2, 2, 1); gfxEllipse(g, x + nw + 4, 36, 1, 1, 0);     // degree ring
  g.setFont(u8g2_font_6x12_tr);
  g.drawStr(x + nw + 8, 44, settings.useF ? "F" : "C");
  // high / low / rain on the right
  char b[12];
  g.setFont(u8g2_font_5x8_tr);
  snprintf(b, sizeof(b), "H%d", (int)lroundf(weatherTemp(w.hi)));  g.drawStr(98, 41, b);
  snprintf(b, sizeof(b), "L%d", (int)lroundf(weatherTemp(w.lo)));  g.drawStr(98, 50, b);
  snprintf(b, sizeof(b), "%d%%", w.rain);                           g.drawStr(98, 59, b);
  g.setFont(u8g2_font_5x7_tr);
  char line[40];
  snprintf(line, sizeof(line), "%.10s %.13s", settings.wxName, weatherCondition(w.code));
  line[25] = 0;
  g.drawStr(2, 63, line);
}

// today's to-dos (or the next day that has any): checkboxes + a done counter.
// A task with a time only uses it for ordering - tasks never ring.
static void drawTaskRow(U8G2& g, const Task& t, int y) {
  g.setFont(u8g2_font_5x7_tr);
  if (t.done) {                                        // filled box + knocked-out tick
    gfxRBox(g, 3, y - 7, 7, 7, 2, 1);
    g.setDrawColor(0);
    gfxLine(g, 4, y - 3, 5, y - 2, 0);
    gfxLine(g, 5, y - 2, 8, y - 6, 0);
    g.setDrawColor(1);
  } else {
    g.drawRFrame(3, y - 7, 7, 7, 2);
  }
  int x = 13;
  if (t.hour != TASK_NO_TIME) {                        // "9:05a" / "21:05"
    struct tm tt = {};
    tt.tm_hour = t.hour; tt.tm_min = t.minute;
    char hm[8], ap[4], ts[10];
    clockParts(tt, hm, sizeof(hm), ap, sizeof(ap));
    if (ap[0]) snprintf(ts, sizeof(ts), "%s%c", hm, ap[0] == 'A' ? 'a' : 'p');
    else snprintf(ts, sizeof(ts), "%s", hm);
    g.drawStr(x, y, ts);
    x += g.getStrWidth(ts) + 2;
  }
  char title[TASK_TITLE_LEN + 1];
  int room = (127 - x) / 5;                            // 5 px per char in this font
  snprintf(title, sizeof(title), "%.*s", room, t.title);
  g.drawStr(x, y, title);
  if (t.done) g.drawHLine(x - 1, y - 3, g.getStrWidth(title) + 1);   // strike-through
}

static void widgetTasks(U8G2& g) {
  if (!clockOk()) { g.setFont(u8g2_font_6x10_tr); centerStr(g, 50, "waiting for time"); return; }
  struct tm day;
  int total = 0, done = 0;
  if (!taskPickDay(day, total, done)) {
    g.setFont(u8g2_font_6x10_tr);
    centerStr(g, 46, "no tasks planned");
    g.setFont(u8g2_font_5x7_tr);
    centerStr(g, 59, "add them in the web app");
    return;
  }
  static const int ry[3] = { 47, 56, 63 };             // row baselines below the divider
  char lab[12];
  taskDayLabel(day, lab, sizeof(lab));
  g.setFont(u8g2_font_5x8_tr);
  g.drawStr(2, 36, lab);
  char cnt[10];
  snprintf(cnt, sizeof(cnt), "%d/%d", done, total);
  g.drawStr(126 - g.getStrWidth(cnt), 36, cnt);
  gfxHLine(g, 1, 39, 126, 1);
  int idx[MAX_TASKS];
  int n = tasksForDay(day, idx, MAX_TASKS);
  int rows = (n > 3) ? 2 : n;                          // room for a "+N more" line when crowded
  for (int r = 0; r < rows; r++) drawTaskRow(g, taskGet(idx[r]), ry[r]);
  if (n > 3) {
    char more[12];
    snprintf(more, sizeof(more), "+%d more", n - 2);
    g.setFont(u8g2_font_5x7_tr);
    g.drawStr(13, ry[2], more);
  }
}

// the next thing on the schedule (alarm or reminder)
static bool nextEntry(int& idx, time_t& ts) {
  idx = -1; ts = 0;
  for (int i = 0; i < alarmsCount(); i++) {
    time_t t;
    if (alarmGet(i).enabled && alarmNextFire(i, t) && (idx < 0 || t < ts)) { idx = i; ts = t; }
  }
  return idx >= 0;
}

static void widgetUpNext(U8G2& g) {
  int idx; time_t ts;
  if (!nextEntry(idx, ts)) {
    g.setFont(u8g2_font_6x10_tr);
    centerStr(g, 46, "nothing scheduled");
    g.setFont(u8g2_font_5x7_tr);
    centerStr(g, 59, "add alarms in the web app");
    return;
  }
  Alarm a = alarmGet(idx);
  struct tm tmv; localtime_r(&ts, &tmv);
  time_t nowT = time(nullptr);
  struct tm td; localtime_r(&nowT, &td);
  bool today = (tmv.tm_yday == td.tm_yday && tmv.tm_year == td.tm_year);
  char tstr[16];
  { char hm[8], ap[4]; clockParts(tmv, hm, sizeof(hm), ap, sizeof(ap)); snprintf(tstr, sizeof(tstr), ap[0] ? "%s %s" : "%s", hm, ap); }
  // type badge
  gfxRBox(g, 4, 33, 13, 13, 3, 1);
  g.setDrawColor(0); g.setFont(u8g2_font_6x10_tr); g.drawStr(7, 43, a.type == AT_REMINDER ? "R" : "A"); g.setDrawColor(1);
  g.setFont(u8g2_font_9x15_tr);
  g.drawStr(22, 46, tstr);
  // "in 3h20m"
  long d = (long)(ts - nowT);
  char in[24];
  if (d < 3600) snprintf(in, sizeof(in), "%ldm", d / 60 + 1);
  else if (d < 86400) snprintf(in, sizeof(in), "%ldh%02ldm", d / 3600, (d % 3600) / 60);
  else snprintf(in, sizeof(in), "%ldd", d / 86400);
  g.setFont(u8g2_font_5x8_tr);
  g.drawStr(126 - g.getStrWidth(in), 44, in);
  char lab[32];
  snprintf(lab, sizeof(lab), "%s%.25s", today ? "" : "tmrw  ", a.label[0] ? a.label : (a.type == AT_REMINDER ? "Reminder" : "Alarm"));
  lab[25] = 0;
  g.setFont(u8g2_font_5x8_tr);
  g.drawStr(4, 60, lab);
}

void widgetDraw(U8G2& g, uint8_t id) {
  g.setDrawColor(1);
  switch (id) {
    case WG_WEATHER: widgetWeather(g); break;
    case WG_DATE:    widgetDate(g);    break;
    case WG_TASKS:   widgetTasks(g);   break;
    case WG_UPNEXT:  widgetUpNext(g);  break;
    default:         widgetClock(g);   break;
  }
}
