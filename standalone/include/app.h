#pragma once
#include <Arduino.h>
#include <time.h>
#include "config.h"
#include "faces.h"
#include "animations.h"
#include "pomodoro.h"
#include "alarms.h"
#include "touch.h"
#include "widgets.h"

// ============================================================
//  app.h - central state machine: face mode, menu, ringing,
//  sleep + the personality-driven "pet" behaviours
// ============================================================

enum UiMode : uint8_t { UI_FACE = 0, UI_WIDGET, UI_MENU, UI_RINGING, UI_NOTIFY, UI_SLEEP };

struct App {
  UiMode mode = UI_FACE;
  // the face the buddy "wants" to wear + a temporary reaction overlay
  Expression face = EX_NEUTRAL;
  Expression reactFace = EX_NEUTRAL;
  uint32_t reactUntil = 0;
  FaceRig rig;
  // what the rig was last pointed at (so we only retarget on change)
  int appliedFace = -1, appliedPack = -1, appliedMood = -1;
  int8_t sideSign = 1;                 // which way side-eye / thinking looks
  uint8_t curFlags = 0;                // flags of the face currently shown
  // scripted personality scenes
  const Scene* script = nullptr;
  uint8_t scriptLen = 0, scriptIdx = 0;
  uint32_t scriptNext = 0, nextScene = 0;
  // sleeping: eyes close first, display powers off at sleepAt
  uint32_t sleepAt = 0;
  // idle: last interaction while on the home face (any time of day) - untouched
  // long enough and the buddy drifts to sleep on its own. dndSlept: it fell
  // asleep during the do-not-disturb window, so it wakes when the window ends
  uint32_t idleSince = 0;
  bool     dndSlept = false;
  // ringing alarm (UI_RINGING) or phone-style reminder notification (UI_NOTIFY)
  bool ringing = false;
  int  ringingIdx = -1;
  char ringLabel[ALARM_LABEL_LEN] = "";
  uint8_t ringMelody = 0;
  uint8_t ringType = AT_ALARM;
  uint32_t notifyAt = 0;
  uint32_t ringStart = 0;              // when the current ring began (auto-stop timer)
  uint8_t  ringAuto = 0;               // consecutive auto-snoozes of the current alarm
  uint32_t ringSig = 0;                // which alarm is ringing (so a list edit cannot disable the wrong one)
  bool     ringWasAsleep = false;      // the ring woke a sleeping buddy -> back to sleep after the ring
  bool     ringWasDeep = false;        // ...and the panel was already off (short show, then dark again)
  UiMode returnMode = UI_FACE;         // where to go back to after ringing / menu
  // emotional sounds + irritation
  uint32_t lastEmo = 0;
  bool emoKick = false;                // replay the emotion sound even if the face did not change
  uint8_t shakeLevel = 0;
  uint32_t lastShake = 0;
  // widget screens (double tap): clock / weather / date / tasks / up next, tap = next
  uint8_t widget = WG_CLOCK;
  // tilt switch emotions: 0 upright, 1 curious, 2 uneasy, 3 upset, 4 crying
  uint8_t  tiltPhase = 0;
  uint32_t tiltQuietUntil = 0;         // after a rocking reaction, let it play out first
  // petting (scratch / stroke)
  TouchEngine touch;
  bool petting = false;
  uint8_t petPhase = 255;
  uint32_t petStart = 0;
  uint32_t nextMurmur = 0;              // when the next soft murmur is due (a pet doesn't buzz nonstop)
  uint8_t agendaTop = 0;
  int8_t  taskOff = 0;                 // Tasks menu browser: days ahead of today (0..31)
  // menu
  uint8_t menuItem = 0;
  uint8_t menuSub = 0;                 // 0 none, 1 faces, 2 style, 3 mood
  uint8_t subView = 0;                 // 0 list, 1 info, 2 sensor test, 4 agenda, 5 tasks
  Expression browseFace = EX_NEUTRAL;
  // gesture suppression right after wake-from-sleep
  bool suppTouch = false;
  // idle schedulers
  uint32_t nextAutoFace = 0, nextBlink = 0, nextLook = 0, bootMs = 0;
  uint32_t lastActive = 0;             // last touch / tilt / alarm / web action (panel dims when idle)
  uint8_t  appliedBr = 0;              // contrast currently sent to the panel
  PomoState lastPomo = PO_IDLE;
};

extern App app;

void appBegin();
void appTick(float dt);

Expression appHomeFace();
Expression appShownFace();               // what the OLED is showing right now               // the resting face of the current mood
void appSetFace(Expression e);          // manual face (cancels scenes)
void appStyleChanged();                 // call after pack/mood changes
void appPet();                          // touch reaction
void appParty();
void appShakeReact();                   // dizzy
void appSleepNow(uint32_t powerOffIn = 1400);   // ms until the panel powers down after the eyes shut
void appWake();
void appSleepToggle();

// alarm ringing control (used by web too)
void appStartRinging(int alarmIdx, const char* label, uint8_t melody);   // full-screen alarm
void appStartNotify(int alarmIdx, const char* label, uint8_t melody);    // slide-in reminder notification
void appStopRinging(bool snooze);

// pomodoro event hook (pomodoro.cpp calls this)
void appPomoEvent(uint8_t evt);
// (v3.13) a pomodoro started from the web app jumps to the face screen so the timer is visible
void appPomoToFront();

// web "action" dispatcher: pet|party|sleep|wake|widgets|face|weather
bool appAction(const char* a);
