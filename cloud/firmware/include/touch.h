#pragma once
#include <Arduino.h>

// ============================================================
//  touch.h - gesture + petting engine
//
//  Fed with raw press/release edges (sensorsEdge). Tells apart:
//    tap / double tap / triple tap : short touches, resolved after a short quiet gap
//    petting                       : a touch longer than 0.45 s (stroking, resting hand)
//                                    or 4+ rapid back-to-back touches (scratching)
//  so scratching a head can never be mistaken for a double tap or a menu hold.
// ============================================================

enum TouchGesture : uint8_t { TG_NONE = 0, TG_TAP, TG_DOUBLE, TG_TRIPLE, TG_PET_START, TG_PET_END };

class TouchEngine {
 public:
  void reset();
  void edge(bool down, uint32_t ms);            // feed edges in order
  TouchGesture poll(uint32_t now);              // call repeatedly until TG_NONE
  bool     petting() const { return pet; }
  bool     touching() const { return down; }
  uint32_t petElapsed(uint32_t now) const { return pet ? now - petStartMs : 0; }
  float    scratchRate(uint32_t now) const;     // contacts per second over the last 2 s

 private:
  void enterPet(uint32_t startMs);
  void push(TouchGesture g);
  bool down = false, pet = false;
  uint32_t downMs = 0, lastUpMs = 0, lastContactMs = 0, petStartMs = 0;
  uint8_t taps = 0;
  uint8_t chain = 0;                 // rapid back-to-back contacts in a row (scratching)
  uint32_t chainStart = 0;
  uint32_t starts[8] = {0};
  uint8_t nStarts = 0;
  TouchGesture q[4] = {TG_NONE};
  uint8_t qn = 0;
};
