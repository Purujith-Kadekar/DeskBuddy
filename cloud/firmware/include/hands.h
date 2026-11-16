// ============================================================
//  hands.h - the third part of the buddy's face
//
//  The buddy's face is three independent parts:
//
//    EYES   the rig (41 expressions x 6 eye styles x 32 variants)
//    MOUTH  16 mouth shapes
//    HANDS  the two little arms (this file)
//
//  The FLASH holds how each part behaves - the shapes and their
//  animations live here.  The Ventuno Q sends the COMBINATION ("smile
//  + wave + close"), so a new look never needs a new firmware table.
//
//  The hands are drawn in the free band UNDER the face (the face is
//  centred at FACE_CY, the panel is 128x64), one on each side.
// ============================================================
#pragma once

#include <Arduino.h>
#include <U8g2lib.h>

enum HandPose : uint8_t {
  HP_REST = 0,        // both arms down
  HP_REST_LOW,        // both arms lower, relaxed
  HP_REST_FOLDED,     // tucked together in front
  HP_WAVE_RIGHT,      // the right arm waves
  HP_WAVE_LEFT,
  HP_WAVE_BOTH,
  HP_RAISE_RIGHT,     // one arm up
  HP_RAISE_LEFT,
  HP_POINT_UP,        // a finger up
  HP_CLAP,            // hands together and apart
  HP_TYPING,          // small fast taps
  HP_PRESENT,         // both palms forward
  HP_RUB_CHIN,        // one hand at the chin
  HP_FACE_PALM,       // one hand over the face
  HP_THUMBS_UP,
  HP_COUNT
};

extern const char* const HAND_NAMES[HP_COUNT];

// the pose for a name, or HP_COUNT when the name is unknown
int handIndexByName(const char* n);

// Draw both arms for a pose.  t is the animation clock (seconds), so
// every pose animates on its own - waving waves, clapping claps.
void handsDraw(U8G2& g, uint8_t pose, float t, int faceCy);
