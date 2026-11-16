#include "sensors.h"
#include "config.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ============================================================
//  sensors.cpp - touch gesture engine v2
//
//  Why the rewrite: v1 had three real-world bugs
//   1) touches lasting 400..600 ms fell in a dead zone -> NOTHING
//      happened (and humans casually touch for 300..600 ms)
//   2) every single tap waited 350 ms for the double-tap window
//      -> everything felt laggy/dead
//   3) pinMode(INPUT) with no pull -> a loose or wrong-pin wire
//      floats and spams ghost taps
//
//  Now:
//   * TAP fires the instant the finger leaves (30..599 ms)
//   * DOUBLE fires the instant the 2nd touch lands
//   * HOLD fires once at 600 ms while still down
//   * glitches < 30 ms dropped, levels debounced 25 ms
//   * internal pull-down/up keeps a floating pin silent
//   * TTP223 modules in TOGGLE mode: TOUCH_TOGGLE_MODE 1
// ============================================================

// ---- debounce ----
static bool     lvlFast = false;       // raw level right now
static bool     lvlStable = false;     // level stable >= TOUCH_DEBOUNCE_MS
static uint32_t lvlSince = 0;          // last time the raw level changed

// ---- press tracking ----
static bool     prevPressed = false;   // stable level on previous tick
static uint32_t pressMs = 0;           // 0 = not pressed
static uint32_t lastTapMs = 0;         // release time of the last clean tap
static bool     holdFired = false;

// ---- TTP223 toggle/latch mode ----
static bool     latchMode = false;
static bool     skipNextEdge = false;
static uint32_t lastEdgeMs = 0;         // bounce guard for latch mode

// ---- event queue (4 deep; appTick drains every 33 ms) ----
static TouchEvt q[4];
static volatile uint8_t qHead = 0, qTail = 0;   // single producer (sensor task) / single consumer (loop)

// ---- raw edge queue for the petting engine (16 deep) ----
struct Edge { bool down; uint32_t ms; };
static Edge     eq[16];
static volatile uint8_t eHead = 0, eTail = 0;
static void pushEdge(bool down, uint32_t ms) {
  uint8_t nxt = (uint8_t)((eHead + 1) & 15);
  if (nxt == eTail) return;                          // full: drop (app drains every frame)
  eq[eHead] = { down, ms };
  eHead = nxt;
}

// ---- tilt / shake ----
// raw edges -> shake. A debounced copy of the level -> "tipped" state, rocking, set-down.
static int      tiltHome = -1;         // resting level (re-learned if it stays tipped for TILT_REHOME_MS)
static int      tiltLastRaw = -1;
static uint8_t  tiltEdges = 0;
static uint32_t tiltWindow = 0;
static volatile bool shakeFlag = false;
static uint32_t lastShakeMs = 0;
static int      tiltCand = -1, tiltStable = -1;      // debounce
static uint32_t tiltCandSince = 0;
static volatile uint32_t tiltAwaySince = 0;          // 0 = upright, else when it was tipped
static volatile uint32_t tiltRetHeld = 0;            // set-down event: how long it had been tipped
static volatile uint8_t  tiltRockFlag = 0;           // 1 rocking, 2 rocking wildly
static uint32_t chgT[8];                             // times of recent stable level changes
static uint8_t  chgN = 0;
static uint32_t lastRockMs = 0, lastWildMs = 0;

// ---- stats for the Sens screen ----
static uint16_t tapCount = 0;
static uint16_t shakeCount = 0;

static void push(TouchEvt e) {
  uint8_t nxt = (uint8_t)((qHead + 1) & 3);
  if (nxt == qTail) return;            // full -> drop newest (never happens)
  q[qHead] = e;
  qHead = nxt;
}

static void sensorsTask(void*);

void sensorsBegin() {
  // TTP223 output is push-pull, so the internal pull is harmless when
  // wired - but it keeps an unconnected/loose pin from floating and
  // spraying ghost touches.
  pinMode(PIN_TOUCH, TOUCH_ACTIVE_LOW ? INPUT_PULLUP : INPUT_PULLDOWN);
  pinMode(PIN_TILT, INPUT_PULLUP);
  lvlFast = lvlStable = prevPressed =
    (digitalRead(PIN_TOUCH) == HIGH) != TOUCH_ACTIVE_LOW;
  lvlSince = millis();
  tiltLastRaw = digitalRead(PIN_TILT);
  tiltHome = tiltLastRaw;              // resting orientation = neutral
  tiltCand = tiltStable = tiltLastRaw;
  tiltCandSince = millis();
  latchMode = (TOUCH_TOGGLE_MODE != 0);
  static bool started = false;
  if (!started) {                      // everything below runs in its own task from now on
    started = true;
    xTaskCreate(sensorsTask, "sensors", 3072, nullptr, 3, nullptr);
  }
}

void sensorsTick() {
  uint32_t ms = millis();

  // ---- level debounce: must stay steady for TOUCH_DEBOUNCE_MS ----
  bool a = (digitalRead(PIN_TOUCH) == HIGH) != TOUCH_ACTIVE_LOW;
  if (a != lvlFast) { lvlFast = a; lvlSince = ms; }
  if (lvlFast != lvlStable && (ms - lvlSince) >= TOUCH_DEBOUNCE_MS)
    lvlStable = lvlFast;

  // ---- edges: press / release, instant events ----
  if (lvlStable != prevPressed) {
    prevPressed = lvlStable;
    uint32_t edgeGap = ms - lastEdgeMs;
    lastEdgeMs = ms;
    if (skipNextEdge) {
      skipNextEdge = false;                        // release of the detect-hold
    } else if (latchMode) {
      // toggle mode: every edge = one touch (bounce < 150 ms is swallowed)
      if (edgeGap >= 150) { push(TE_TAP); tapCount++; pushEdge(true, ms - 60); pushEdge(false, ms); }
    } else if (lvlStable) {
      pushEdge(true, ms);
      // PRESS: double-tap fires right here (no delay anywhere)
      if (lastTapMs != 0 && (ms - lastTapMs) < TOUCH_DOUBLE_MS)
        push(TE_DOUBLE);
    } else {
      pushEdge(false, ms);
      // RELEASE: instant tap if duration is in range
      uint32_t dur = pressMs ? (ms - pressMs) : 0;
      if (dur >= TOUCH_MIN_MS && dur < TOUCH_HOLD_MS) {
        push(TE_TAP);
        tapCount++;
        lastTapMs = ms;
      }
      // dur < TOUCH_MIN_MS  -> electrical glitch, ignored
      // dur >= TOUCH_HOLD   -> TE_HOLD already fired while down
    }
  }

  // ---- press duration tracking (hold + latch auto-detect) ----
  if (lvlStable) {
    if (pressMs == 0) { pressMs = ms; holdFired = false; }
    if (!latchMode && !holdFired && (ms - pressMs) >= TOUCH_HOLD_MS) {
      push(TE_HOLD);
      holdFired = true;
    }
#if TOUCH_LATCH_MS > 0
    if (!latchMode && (ms - pressMs) >= TOUCH_LATCH_MS) {
      // level stuck "pressed" for 90 s: the module is in toggle mode
      latchMode = true;
      skipNextEdge = true;
      Serial.println(F("[TOUCH] TTP223 toggle mode detected - edge taps"));
    }
#endif
  } else {
    pressMs = 0;
    holdFired = false;
  }

  // ---- tilt / shake ----
  int raw = digitalRead(PIN_TILT);
  if (raw != tiltLastRaw) {                          // raw edges: 4 within 0.8 s = a shake
    tiltLastRaw = raw;
    if ((ms - tiltWindow) > 800) { tiltWindow = ms; tiltEdges = 0; }
    if (++tiltEdges >= 4 && (ms - lastShakeMs) > 1500) {
      shakeFlag = true;
      shakeCount++;
      lastShakeMs = ms;
      tiltEdges = 0;
      chgN = 0;                                      // a shake is not "rocking"
    }
  }

  // debounced level: a ball switch rattles, so a level only counts once it has held a moment
  if (raw != tiltCand) { tiltCand = raw; tiltCandSince = ms; }
  if (tiltCand != tiltStable && (ms - tiltCandSince) >= TILT_DEBOUNCE_MS) {
    tiltStable = tiltCand;
    if (tiltStable == tiltHome) {                    // set back down
      if (tiltAwaySince) { uint32_t h = ms - tiltAwaySince; tiltRetHeld = h ? h : 1; }
      tiltAwaySince = 0;
    } else {
      tiltAwaySince = ms ? ms : 1;                   // tipped away from rest
    }
    // rocking = several direction changes in a few seconds (a shake is faster, a pick-up is 2)
    uint8_t keep = 0;
    for (uint8_t i = 0; i < chgN; i++)
      if (ms - chgT[i] <= TILT_ROCK_WIN_MS) chgT[keep++] = chgT[i];
    chgN = keep;
    if (chgN < 8) chgT[chgN++] = ms;
    else { for (uint8_t i = 1; i < 8; i++) chgT[i - 1] = chgT[i]; chgT[7] = ms; }
    if ((ms - lastShakeMs) > 3000) {
      if (chgN >= TILT_ROCK_WILD_N && (ms - lastWildMs) > 3500) {   // keeps going: gets dizzy
        tiltRockFlag = 2;
        lastWildMs = lastRockMs = ms;
        chgN = 0;
      } else if (chgN >= TILT_ROCK_N && (ms - lastRockMs) > 3500) { // a few swings: "wheee"
        tiltRockFlag = 1;
        lastRockMs = ms;                             // (the count keeps running toward "wild")
      }
    }
  }
  // tipped for a very long time = the buddy was simply put down that way: learn it as "upright"
  if (tiltAwaySince && (ms - tiltAwaySince) >= TILT_REHOME_MS) {
    tiltHome = tiltStable;
    tiltAwaySince = 0;
    Serial.println(F("[TILT] new resting orientation learned"));
  }
}

static void sensorsTask(void*) {
  for (;;) {
    sensorsTick();
    vTaskDelay(pdMS_TO_TICKS(5) > 0 ? pdMS_TO_TICKS(5) : 1);
  }
}

TouchEvt sensorsTouch() {
  if (qTail == qHead) return TE_NONE;
  TouchEvt e = q[qTail];
  qTail = (uint8_t)((qTail + 1) & 3);
  return e;
}

bool sensorsShake() {
  bool s = shakeFlag;
  shakeFlag = false;
  return s;
}

int  sensorsTilt()   { return (tiltStable == tiltHome) ? 0 : ((tiltStable == LOW) ? 1 : -1); }
uint32_t sensorsTiltHeldMs() { uint32_t a = tiltAwaySince; return a ? (uint32_t)(millis() - a) : 0; }
uint32_t sensorsTiltReturned() { uint32_t h = tiltRetHeld; tiltRetHeld = 0; return h; }
uint8_t  sensorsTiltRock() { uint8_t r = tiltRockFlag; tiltRockFlag = 0; return r; }
bool sensorsTouched(){ return lvlStable; }

bool sensorsEdge(bool& down, uint32_t& ms) {
  if (eTail == eHead) return false;
  down = eq[eTail].down; ms = eq[eTail].ms;
  eTail = (uint8_t)((eTail + 1) & 15);
  return true;
}

bool     sensorsLatchMode()  { return latchMode; }
uint8_t  sensorsTouchPin()  { return PIN_TOUCH; }
uint8_t  sensorsTiltPin()   { return PIN_TILT; }
uint16_t sensorsTapCount()  { return tapCount; }
uint16_t sensorsShakeCount(){ return shakeCount; }
