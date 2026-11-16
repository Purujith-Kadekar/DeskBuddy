// ============================================================
//  hands.cpp - the buddy's two little arms
//
//  The third part of the face.  The FLASH holds how each pose behaves
//  (the shapes and their animations are here); the Ventuno Q only
//  NAMES the pose it wants, so a new combination never needs a new
//  table.
//
//  The arms live in the free band at the sides of the panel (the face
//  is centred at FACE_CX=64 and the eyes span roughly x=20..108), so
//  the hands never cover the eyes or the mouth.
// ============================================================
#include <Arduino.h>
#include <math.h>
#include "hands.h"
#include "faces.h"

const char* const HAND_NAMES[HP_COUNT] = {
  "rest", "rest-low", "rest-folded", "wave-right", "wave-left", "wave-both",
  "raise-right", "raise-left", "point-up", "clap", "typing", "present",
  "rub-chin", "facepalm", "thumbs-up"
};

int handIndexByName(const char* n) {
  if (!n) return HP_COUNT;
  for (uint8_t i = 0; i < HP_COUNT; i++)
    if (!strcmp(HAND_NAMES[i], n)) return (int)i;
  return HP_COUNT;
}

// ---------------------------------------------------------------- drawing
static void mitten(U8G2& g, int cx, int cy, bool thumbIn) {
  // the palm
  g.drawDisc(cx, cy, 3);
  // the thumb
  if (thumbIn) g.drawBox(cx - 5, cy - 1, 3, 3);
  else          g.drawBox(cx + 2, cy - 1, 3, 3);
}

static void arm(U8G2& g, bool left, int hx, int hy) {
  int sx = left ? 0 : 127;
  g.drawLine(sx, hy + 2, hx, hy);
}

// one arm: where it sits, and how it moves
static void drawArm(U8G2& g, bool left, uint8_t pose, float t) {
  int hx = left ? 9 : 118;         // the resting hand
  int hy = 58;
  bool thumbIn = left;              // the thumbs point towards the middle
  float w = 0.0f;

  switch (pose) {
    case HP_REST:
      hx = left ? 9 : 118; hy = 58; break;
    case HP_REST_LOW:
      hx = left ? 8 : 119; hy = 61; break;
    case HP_REST_FOLDED:
      hx = left ? 46 : 81; hy = 61; thumbIn = !left; break;
    case HP_WAVE_LEFT:
      if (left) { hy = 30; w = sinf(t * 7.0f) * 4.0f; thumbIn = false; }
      else { hx = 118; hy = 58; }
      break;
    case HP_WAVE_RIGHT:
      if (!left) { hy = 30; w = sinf(t * 7.0f) * 4.0f; thumbIn = true; }
      else { hx = 9; hy = 58; }
      break;
    case HP_WAVE_BOTH:
      hy = 32; w = sinf(t * 7.0f) * 4.0f; thumbIn = left; break;
    case HP_RAISE_LEFT:
      if (left) { hy = 22; thumbIn = false; }
      else { hx = 118; hy = 58; }
      break;
    case HP_RAISE_RIGHT:
      if (!left) { hy = 22; thumbIn = true; }
      else { hx = 9; hy = 58; }
      break;
    case HP_POINT_UP:
      if (!left) { hy = 26; thumbIn = true; }
      else { hx = 9; hy = 58; }
      break;
    case HP_CLAP:
      hy = 56; thumbIn = !left;
      // the hands move together and apart
      hx = (left ? 56 : 72) + (left ? -1 : 1) * (int)(sinf(t * 6.0f) * 6.0f);
      break;
    case HP_TYPING:
      hy = 58 + (int)(sinf(t * 14.0f) * 2.0f);
      hx = left ? 40 : 87; thumbIn = left;
      break;
    case HP_PRESENT:
      hx = left ? 14 : 113; hy = 52; thumbIn = left; break;
    case HP_RUB_CHIN:
      if (!left) { hx = 70; hy = 50; thumbIn = true; }
      else { hx = 9; hy = 58; }
      break;
    case HP_FACE_PALM:
      if (!left) { hx = 84; hy = 34; thumbIn = true; }
      else { hx = 9; hy = 58; }
      break;
    case HP_THUMBS_UP:
      if (!left) { hx = 112; hy = 34; thumbIn = true; }
      else { hx = 9; hy = 58; }
      break;
    default:
      break;
  }
  arm(g, left, hx, hy);
  mitten(g, hx + (int)w, hy, thumbIn);
}

void handsDraw(U8G2& g, uint8_t pose, float t, int faceCy) {
  if (pose >= HP_COUNT) pose = HP_REST;
  g.setDrawColor(1);
  // both arms use the same pose; the pose decides which side is active
  uint8_t p = pose;
  if (p == HP_WAVE_LEFT || p == HP_RAISE_LEFT) {
    drawArm(g, true, p, t);
    drawArm(g, false, HP_REST, t);
    return;
  }
  if (p == HP_WAVE_RIGHT || p == HP_RAISE_RIGHT || p == HP_POINT_UP ||
      p == HP_RUB_CHIN || p == HP_FACE_PALM || p == HP_THUMBS_UP) {
    drawArm(g, true, HP_REST, t);
    drawArm(g, false, p, t);
    return;
  }
  drawArm(g, true, p, t);
  drawArm(g, false, p, t);
}
