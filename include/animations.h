#pragma once
#include <Arduino.h>

// ============================================================
//  animations.h - the face "rig": every eye parameter is a
//  spring chasing its target, so expressions MORPH into each
//  other with a little overshoot (squash & stretch) instead of
//  hard-cutting.  Also: gaze, blink, breathing, wobble, squash.
// ============================================================

#define EYE_NP 10          // w,h,r,dx,dy,lid,lidB,slope,cheek,shine
enum EyeParam : uint8_t {
  P_W = 0, P_H, P_R, P_DX, P_DY, P_LID, P_LIDB, P_SLOPE, P_CHEEK, P_SHINE
};

struct Spring {
  float x = 0, v = 0;
};

struct FaceRig {
  // gaze (-1..1), springs give it a smooth glide
  float  lookTX = 0, lookTY = 0;
  Spring lookX, lookY;
  // squash & stretch impulse (1 = rest)
  Spring squash;
  // blink 0 (open) .. 1 (closed)
  float blink = 0, blinkTarget = 0, blinkSpeed = 1.0f;
  // breathing / wobble
  float breathePhase = 0, breatheY = 0, breatheAmp = 1.0f;
  float wobble = 0, wobblePhase = 0;
  float tempo = 1.0f;           // spring speed multiplier (mood)
  // eye morph state: 2 eyes (0 = left, 1 = right)
  float cur[2][EYE_NP];
  float vel[2][EYE_NP];
  float tgt[2][EYE_NP];
  // study glasses: 0 = off (lifted above the face), 1 = on. Spring => little bounce when they land
  Spring glasses;
  float  glassesT = 0;
  // petting: head leans into the hand (0..1)
  Spring lean;
  float  leanT = 0;
  // mouth: shape swaps through a quick shrink; mouthS = size 0..1
  uint8_t mouth = 0, mouthT = 0;
  Spring  mouthS;
  // blush fades in/out
  float blush = 0, blushT = 0;
  // discrete things that swap
  uint8_t glyph = 0, glyphT = 0;   // 0 none, 1 spiral, 2 X
  uint8_t acc = 0;                 // accessory (tear / sweat / zzz)
  uint8_t flags = 0;
  float   t = 0;                   // running time (accessories, jiggle)
  float   spin = 0;                // spiral phase
};

void rigInit(FaceRig& r);
void rigCloseEyes(FaceRig& r);                 // for wake-up / boot "eyes open" animation
void rigTick(FaceRig& r, float dt);
void rigBlink(FaceRig& r, float dt);           // advances blink open/close
void rigImpulse(FaceRig& r, float amount);     // squash bounce (negative = squash down)
void rigLookAt(FaceRig& r, float x, float y);
