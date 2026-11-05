#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// ============================================================
//  widgets.h - the info screens you scroll through (single tap) in widget mode.
//  The buddy's little eyes are drawn on top of every one of them by app.cpp;
//  these functions only draw the content area below the eyes (y >= 28).
// ============================================================

enum WidgetId : uint8_t {
  WG_CLOCK = 0, WG_WEATHER, WG_DATE, WG_TASKS, WG_UPNEXT, WG_COUNT
};
#define WG_ALL_MASK 0x1F

void    widgetDraw(U8G2& g, uint8_t id);
uint8_t widgetNext(uint8_t cur, uint8_t mask);        // next enabled widget after cur (wraps)
const char* widgetName(uint8_t id);
