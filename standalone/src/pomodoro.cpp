#include "pomodoro.h"
#include "app.h"          // for appPomoEvent notifications

enum PomoEvt : uint8_t { PEV_FOCUS_DONE = 1, PEV_BREAK_DONE, PEV_SKIPPED };

static PomoState st = PO_IDLE;
static uint16_t focusMin = 25, breakMin = 5;
static uint8_t  cycles = 0;
static bool     paused = false;
static uint32_t phaseEnd = 0;        // ms epoch when phase ends
static uint32_t pauseLeft = 0;       // s remaining while paused

void pomoBegin() { st = PO_IDLE; cycles = 0; paused = false; }

static void enter(PomoState s) {
  st = s;
  paused = false;
  phaseEnd = millis() + ((s == PO_FOCUS ? focusMin : breakMin) * 60UL) * 1000UL;
}

void pomoStart() {
  if (st == PO_IDLE) enter(PO_FOCUS);
  else if (paused) { // resume
    phaseEnd = millis() + pauseLeft * 1000UL;
    paused = false;
  }
}

void pomoPause() {
  if (st == PO_IDLE || paused) return;
  pauseLeft = pomoStatus().remaining;
  paused = true;
}

void pomoReset() { st = PO_IDLE; cycles = 0; paused = false; }

void pomoSkip() {
  if (st == PO_IDLE) return;
  if (st == PO_FOCUS) { cycles++; appPomoEvent(PEV_SKIPPED); enter(PO_BREAK); }
  else enter(PO_FOCUS);
}

void pomoTick() {
  if (st == PO_IDLE || paused) return;
  if ((int32_t)(millis() - phaseEnd) < 0) return;
  if (st == PO_FOCUS) {
    cycles++;
    appPomoEvent(PEV_FOCUS_DONE);
    enter(PO_BREAK);
  } else {
    appPomoEvent(PEV_BREAK_DONE);
    st = PO_IDLE;
  }
}

PomoState pomoState() { return st; }
bool pomoRunning() { return st != PO_IDLE; }

PomodoroStatus pomoStatus() {
  PomodoroStatus s;
  s.state = st;
  s.cycles = cycles;
  s.paused = paused;
  if (st == PO_IDLE) { s.remaining = 0; s.total = 0; }
  else {
    s.total = (st == PO_FOCUS ? focusMin : breakMin) * 60UL;
    s.remaining = paused ? pauseLeft
      : (uint32_t)((int32_t)(phaseEnd - millis()) / 1000);
    if ((int32_t)s.remaining < 0) s.remaining = 0;
  }
  return s;
}

void pomoSetLens(uint16_t f, uint16_t b) {
  if (f >= 1 && f <= 180) focusMin = f;
  if (b >= 1 && b <= 90) breakMin = b;
}

uint16_t pomoFocusMin() { return focusMin; }
uint16_t pomoBreakMin() { return breakMin; }
