#pragma once
#include <Arduino.h>

// true while new firmware is streaming in; the buddy pauses its app so the
// upload gets the radio and the main loop to itself
extern volatile bool ota_busy;

void otaStart();   // once, when WiFi is up (calling it again is a no-op)
void otaTick();    // every pass of loop()
