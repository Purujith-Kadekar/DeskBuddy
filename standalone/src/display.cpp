#include "config.h"
#include "display.h"
#include <Wire.h>

static U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

void displayBegin() {
  Wire.begin(PIN_OLED_SDA, PIN_OLED_SCL);
  Wire.setClock(400000);
  u8g2.setBusClock(400000);
  u8g2.begin();
  u8g2.setContrast(180);
  u8g2.clearBuffer();
  u8g2.sendBuffer();
}

U8G2& display() { return u8g2; }

void displaySetBrightness(uint8_t b) { u8g2.setContrast(b); }

void displaySleep(bool on) { u8g2.setPowerSave(on ? 1 : 0); }
