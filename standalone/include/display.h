#pragma once
#include <U8g2lib.h>

void displayBegin();                 // Wire + U8g2 init, 400kHz I2C
U8G2& display();                     // access the u8g2 instance
void displaySetBrightness(uint8_t b);// 40..255 (setContrast)
void displaySleep(bool on);          // OLED power save / wake
