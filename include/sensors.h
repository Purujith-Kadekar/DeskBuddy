#pragma once
#include <Arduino.h>

// ============================================================
//  sensors.h - touch gestures + tilt/shake detection (v2)
//
//  Gesture timings (all zero-lag by design):
//   TE_TAP    fires the INSTANT the finger leaves (30..599 ms)
//   TE_DOUBLE fires the INSTANT a 2nd touch lands (< 320 ms gap)
//   TE_HOLD   fires once at 600 ms while still touching
// ============================================================

enum TouchEvt : uint8_t {
  TE_NONE = 0,
  TE_TAP,        // clean short press - instant on release
  TE_DOUBLE,     // 2nd press within TOUCH_DOUBLE_MS of a tap
  TE_HOLD        // press >= TOUCH_HOLD_MS - fires while held
};

void     sensorsBegin();         // also starts the sensor task (5 ms period)
void     sensorsTick();          // one sensor step - called by the sensor task, not by loop()
TouchEvt sensorsTouch();         // consume one queued event
bool     sensorsShake();         // consume shake event
int      sensorsTilt();          // -1 / 0 / +1 debounced tilt direction (0 = upright)
uint32_t sensorsTiltHeldMs();    // how long it has been tipped away from upright (0 = upright)
uint32_t sensorsTiltReturned();  // consume: just set back upright -> how long it was tipped (ms), else 0
uint8_t  sensorsTiltRock();      // consume: 0 none, 1 rocked, 2 rocked wildly
bool     sensorsTouched();       // debounced touch level right now
bool     sensorsEdge(bool& down, uint32_t& ms);   // raw debounced press/release events, in order (petting engine)

// live diagnostics (Menu -> Sens screen)
bool     sensorsLatchMode();     // TTP223 detected in toggle/latch mode
uint8_t  sensorsTouchPin();
uint8_t  sensorsTiltPin();
uint16_t sensorsTapCount();      // clean taps since boot
uint16_t sensorsShakeCount();
