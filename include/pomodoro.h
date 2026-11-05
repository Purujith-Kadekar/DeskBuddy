#pragma once
#include <Arduino.h>

// ============================================================
//  pomodoro.h - focus/break timer with cycles
// ============================================================

enum PomoState : uint8_t { PO_IDLE = 0, PO_FOCUS, PO_BREAK };

struct PomodoroStatus {
  PomoState state;
  uint32_t remaining;   // seconds
  uint32_t total;       // phase length seconds
  uint8_t  cycles;      // completed focus sessions
  bool     paused;
};

void pomoBegin();
void pomoTick();                          // call every loop (fires events)
void pomoStart();                         // begin focus (or resume)
void pomoPause();
void pomoReset();                         // back to idle
void pomoSkip();                          // jump to next phase
PomoState pomoState();
bool pomoRunning();
PomodoroStatus pomoStatus();
void pomoSetLens(uint16_t focusMin, uint16_t breakMin);
uint16_t pomoFocusMin();
uint16_t pomoBreakMin();
