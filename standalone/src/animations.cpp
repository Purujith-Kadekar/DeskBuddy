#include "animations.h"
#include <math.h>

// ============================================================
//  animations.cpp - spring rig (identical maths in the web
//  mirror: tools/face_renderer.js)
// ============================================================

#define EYE_K 125.0f      // spring stiffness
#define EYE_D 13.8f       // damping (slightly under-damped = lively overshoot)

static const float NEUTRAL[EYE_NP] = { 24, 30, .45f, 22, 0, 0, 0, 0, 0, .5f };

void rigInit(FaceRig& r) {
  r = FaceRig();
  r.squash.x = 1.0f;
  r.glasses.x = 0.0f;
  r.mouthS.x = 1.0f;
  for (int e = 0; e < 2; e++)
    for (int p = 0; p < EYE_NP; p++) {
      r.cur[e][p] = r.tgt[e][p] = NEUTRAL[p];
      r.vel[e][p] = 0;
    }
}

void rigCloseEyes(FaceRig& r) {
  for (int e = 0; e < 2; e++) { r.cur[e][P_H] = 2; r.vel[e][P_H] = 0; }
}

void rigImpulse(FaceRig& r, float amount) { r.squash.v += amount * 6.0f; }

void rigLookAt(FaceRig& r, float x, float y) {
  r.lookTX = constrain(x, -1.3f, 1.3f);
  r.lookTY = constrain(y, -1.3f, 1.3f);
}

void rigBlink(FaceRig& r, float dt) {
  // closes fast, opens a touch slower
  float sp = r.blinkSpeed;
  if (r.blinkTarget > r.blink) {
    r.blink += dt * 14.0f * sp;
    if (r.blink >= 1.0f) { r.blink = 1.0f; r.blinkTarget = 0; }
  } else if (r.blink > 0) {
    r.blink -= dt * 9.0f * sp;
    if (r.blink < 0) r.blink = 0;
  }
}

void rigTick(FaceRig& r, float dt) {
  if (dt > 0.05f) dt = 0.05f;
  r.t += dt;
  r.spin += dt * 6.0f;
  float tp = r.tempo;
  float k = EYE_K * tp * tp, d = EYE_D * tp;

  // glyph swap: close the eyes first, swap when nearly shut
  bool swapping = (r.glyph != r.glyphT);

  for (int e = 0; e < 2; e++)
    for (int p = 0; p < EYE_NP; p++) {
      float target = r.tgt[e][p];
      if (swapping && p == P_H) target = 2.0f;
      float a = (target - r.cur[e][p]) * k - r.vel[e][p] * d;
      r.vel[e][p] += a * dt;
      r.cur[e][p] += r.vel[e][p] * dt;
    }
  if (swapping && r.cur[0][P_H] < 6.0f) r.glyph = r.glyphT;

  // gaze springs (softer than the eyes)
  float lk = 120.0f * tp * tp, ld = 15.0f * tp;
  r.lookX.v += ((r.lookTX - r.lookX.x) * lk - r.lookX.v * ld) * dt;
  r.lookX.x += r.lookX.v * dt;
  r.lookY.v += ((r.lookTY - r.lookY.x) * lk - r.lookY.v * ld) * dt;
  r.lookY.x += r.lookY.v * dt;

  // squash spring (rest = 1)
  float sk = 170.0f, sd = 13.0f;
  r.squash.v += ((1.0f - r.squash.x) * sk - r.squash.v * sd) * dt;
  r.squash.x += r.squash.v * dt;
  r.squash.x = constrain(r.squash.x, 0.55f, 1.5f);

  // head lean into a petting hand
  r.lean.v += ((r.leanT - r.lean.x) * 60.0f - r.lean.v * 9.0f) * dt;
  r.lean.x += r.lean.v * dt;

  // mouth: shrink -> swap shape -> grow
  {
    float tgt = (r.mouth == r.mouthT) ? 1.0f : 0.0f;
    r.mouthS.v += ((tgt - r.mouthS.x) * 260.0f - r.mouthS.v * 22.0f) * dt;
    r.mouthS.x += r.mouthS.v * dt;
    if (r.mouth != r.mouthT && r.mouthS.x < 0.18f) r.mouth = r.mouthT;
  }

  // glasses drop on / lift off (springy)
  r.glasses.v += ((r.glassesT - r.glasses.x) * 90.0f - r.glasses.v * 11.0f) * dt;
  r.glasses.x += r.glasses.v * dt;

  // breathing
  r.breathePhase += dt * 1.6f * tp;
  r.breatheY = sinf(r.breathePhase) * r.breatheAmp;

  // wobble impulse decays; continuous wobble handled in drawFace via flags
  r.wobblePhase += dt * 14.0f;
  if (r.wobble > 0) { r.wobble -= dt * 0.45f; if (r.wobble < 0) r.wobble = 0; }

  // blush glides
  r.blush += (r.blushT - r.blush) * fminf(1.0f, dt * 6.0f);
}
