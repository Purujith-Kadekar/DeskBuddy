#include "touch.h"
#include "config.h"

void TouchEngine::reset() {
  down = pet = false;
  downMs = lastUpMs = lastContactMs = petStartMs = 0;
  taps = 0; nStarts = 0; qn = 0; chain = 0; chainStart = 0;
}

void TouchEngine::push(TouchGesture g) {
  if (qn < 4) q[qn++] = g;
}

void TouchEngine::enterPet(uint32_t startMs) {
  pet = true;
  petStartMs = startMs;
  taps = 0;                                       // whatever taps were pending were just scratching
  push(TG_PET_START);
}

void TouchEngine::edge(bool d, uint32_t ms) {
  if (d) {
    if (!pet && taps > 0 && ms - lastUpMs >= TOUCH_MULTI_GAP_MS) {      // the previous tap(s) already had their quiet gap
      push(taps == 1 ? TG_TAP : taps == 2 ? TG_DOUBLE : TG_TRIPLE);
      taps = 0;
    }
    down = true;
    downMs = ms;
    lastContactMs = ms;
    // rapid back-to-back contacts = scratching; taps with a normal pause between never chain
    if (lastUpMs && ms - lastUpMs < TOUCH_SCRATCH_GAP_MS && chain > 0) chain++;
    else { chain = 1; chainStart = ms; }
    // contact times for the scratch-rate estimate
    uint8_t keep = 0;
    for (uint8_t i = 0; i < nStarts; i++)
      if (ms - starts[i] <= TOUCH_SCRATCH_WIN_MS) starts[keep++] = starts[i];
    nStarts = keep;
    if (nStarts < 8) starts[nStarts++] = ms;
    else { for (uint8_t i = 1; i < 8; i++) starts[i - 1] = starts[i]; starts[7] = ms; }
    if (!pet && chain >= TOUCH_SCRATCH_N) enterPet(chainStart);         // scratching
  } else {
    down = false;
    lastUpMs = ms;
    lastContactMs = ms;
    if (!pet) {
      if (ms - downMs >= TOUCH_PET_HOLD_MS) enterPet(downMs);           // a long stroke that ended between frames
      else if (taps < 9) taps++;
    }
  }
}

TouchGesture TouchEngine::poll(uint32_t now) {
  if (!pet && down && now - downMs >= TOUCH_PET_HOLD_MS) enterPet(downMs);     // resting / stroking hand

  if (!down && !pet && taps > 0 && now - lastUpMs >= TOUCH_MULTI_GAP_MS) {
    push(taps == 1 ? TG_TAP : taps == 2 ? TG_DOUBLE : TG_TRIPLE);
    taps = 0;
  }
  if (pet && !down && now - lastContactMs >= TOUCH_PET_END_MS) {
    pet = false;
    nStarts = 0;
    chain = 0;
    push(TG_PET_END);
  }
  if (qn == 0) return TG_NONE;
  TouchGesture g = q[0];
  for (uint8_t i = 1; i < qn; i++) q[i - 1] = q[i];
  qn--;
  return g;
}

float TouchEngine::scratchRate(uint32_t now) const {
  int n = 0;
  for (uint8_t i = 0; i < nStarts; i++)
    if (now - starts[i] <= 2000) n++;
  return n / 2.0f;
}
