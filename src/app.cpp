#include "app.h"
#include "display.h"
#include "audio.h"
#include "alarms.h"
#include "tasks.h"
#include "clocksync.h"
#include "sensors.h"
#include "settings.h"
#include "pomodoro.h"
#include "weather.h"
#include <WiFi.h>

App app;

// ============================================================
//  helpers
// ============================================================

static uint32_t rand32() { return esp_random(); }
static uint32_t rrange(uint32_t a, uint32_t b) { return b <= a ? a : a + rand32() % (b - a + 1); }
static float frand(float lo, float hi) { return lo + (hi - lo) * ((rand32() % 10000) / 9999.0f); }

static EyePack curPack() { return (EyePack)(settings.pack < PK_COUNT ? settings.pack : 0); }
static Mood curMood() { return (Mood)(settings.mood < MD_COUNT ? settings.mood : 0); }
static const MoodDef& M() { return MOODS[curMood()]; }

// night time or do-not-disturb window: emotion sounds are muted
// (alarms, reminders and the timer are not - the cuckoo stays silent in DND)
static bool quietNow() {
  if (dndActiveNow()) return true;
  if (!clockOk()) return false;
  time_t t = time(nullptr);
  struct tm tmv; localtime_r(&t, &tmv);
  return tmv.tm_hour >= QUIET_START_HOUR || tmv.tm_hour < QUIET_END_HOUR;
}

static bool soundOk() { return M().sfx && settings.volume > 0 && !quietNow(); }

// small UI sound, muted by the "silent" mood and at night
static void sfx(SfxId s) { if (soundOk()) audioPlaySfx(s); }

// the voice of an expression: one sound per emotion
static void emoSound(Expression e, bool kick) {
  if (!soundOk()) return;
  if (app.mode == UI_RINGING || app.mode == UI_NOTIFY || app.mode == UI_SLEEP) return;
  uint32_t now = millis();
  if (now - app.lastEmo < (kick ? 250UL : 1200UL)) return;
  app.lastEmo = now;
  audioPlayEmotion((uint8_t)e);
}

Expression appHomeFace() { return (Expression)M().home; }

static Expression effectiveFace() {
  if (app.mode == UI_RINGING) return EX_PANIC;
  if (app.mode == UI_NOTIFY) return (millis() - app.notifyAt < 1700) ? EX_SURPRISED : EX_HAPPY;
  if (app.mode == UI_MENU && app.menuSub == 1) return app.browseFace;
  if (app.mode == UI_MENU && app.menuSub == 2) {
    static const Expression pv[] = { EX_NEUTRAL, EX_HAPPY, EX_SURPRISED, EX_SAD, EX_ANGRY, EX_LOVE };
    return pv[(millis() / 1700) % 6];
  }
  if (app.mode == UI_MENU && app.menuSub == 3) {
    const MoodDef& m = M();
    return (Expression)m.pool[(millis() / 1900) % m.poolN];
  }
  if (millis() < app.reactUntil) return app.reactFace;
  return app.face;
}

Expression appShownFace() { return app.mode == UI_SLEEP ? EX_SLEEP : effectiveFace(); }

// ============================================================
//  public face control
// ============================================================

// urgent = an "I'm being annoyed" reaction: its sound may never be swallowed by the rate limit
static void react(Expression e, uint32_t ms, bool urgent = false) {
  app.reactFace = e;
  app.reactUntil = millis() + ms;
  app.script = nullptr;
  app.emoKick = true;
  if (urgent) app.lastEmo = 0;
}

void appSetFace(Expression e) {
  if (e >= EX_COUNT) return;
  app.face = e;
  app.script = nullptr;             // a manual choice cancels any running scene
  app.emoKick = true;
}

void appStyleChanged() {
  app.script = nullptr;
  app.face = appHomeFace();
  app.reactUntil = 0;
  uint32_t now = millis();
  app.nextAutoFace = now + rrange(M().chgMin, M().chgMax);
  app.nextScene = now + (M().sceneMax ? rrange(M().sceneMin, M().sceneMax) : 0);
  app.appliedMood = -1;             // force retarget
}

void appPet() {
  const MoodDef& m = M();
  react((Expression)m.pet[rand32() % m.petN], PET_REACT_MS);
  rigImpulse(app.rig, -2.6f * m.bounce);
}

void appParty() {
  react(EX_PARTY, 4000);
  rigImpulse(app.rig, -2.0f);
  sfx(SFX_FANFARE);
}

// shaking = irritating it: surprised -> dizzy -> angry -> crying (resets after 12 s of peace)
void appShakeReact() {
  uint32_t now = millis();
  app.dndIdle = now;                     // interaction: restarts the DND idle clock
  if (now - app.lastShake > 12000UL) app.shakeLevel = 0;
  app.lastShake = now;
  if (app.shakeLevel < 250) app.shakeLevel++;
  switch (app.shakeLevel) {
    case 1:  react(EX_SURPRISED, 1400, true); break;
    case 2:  react(EX_DIZZY, DIZZY_MS, true); app.rig.wobble = 1.0f; break;
    case 3:  react(EX_ANGRY, 2600, true); break;
    default: react(EX_CRY, 3400, true); break;
  }
  rigImpulse(app.rig, -2.2f);
}

void appSleepNow(uint32_t powerOffIn) {
  if (app.mode == UI_SLEEP) return;
  app.mode = UI_FACE;
  app.menuSub = 0; app.subView = 0; app.menuItem = 0;
  app.face = EX_SLEEP;
  app.reactUntil = 0;
  app.script = nullptr;
  app.emoKick = true;
  app.sleepAt = millis() + powerOffIn;    // let the eyes drift shut first
  audioStop();
}

void appWake() {
  app.lastActive = millis();
  app.sleepAt = 0;
  app.dndSlept = false;                  // awake = no longer "we put it to bed"
  app.dndIdle = millis();
  if (app.mode == UI_SLEEP) {
    displaySleep(false);
    app.mode = UI_FACE;
    rigCloseEyes(app.rig);          // eyes open from shut: a nice wake-up
    rigImpulse(app.rig, -1.8f);
    if (soundOk()) audioPlaySfx(SFX_WAKE);
  }
  if (app.face == EX_SLEEP) app.face = appHomeFace();
}

void appSleepToggle() {
  if (app.mode == UI_SLEEP || app.sleepAt) appWake();
  else appSleepNow();
}

// ============================================================
//  alarm (full screen) + reminder (phone notification)
// ============================================================

static void ringCommon(int alarmIdx, const char* label, uint8_t melody, uint8_t type) {
  displaySleep(false);
  app.sleepAt = 0;
  app.ringing = true;
  app.ringStart = millis();
  app.ringingIdx = alarmIdx;
  strncpy(app.ringLabel, label ? label : "", sizeof(app.ringLabel) - 1);
  app.ringLabel[sizeof(app.ringLabel) - 1] = 0;
  app.ringMelody = melody;
  app.ringType = type;
  if (app.mode == UI_FACE || app.mode == UI_WIDGET) app.returnMode = app.mode;
  else app.returnMode = UI_FACE;
  app.menuSub = 0; app.subView = 0;
  app.rig.wobble = 1.0f;
}

void appStartRinging(int alarmIdx, const char* label, uint8_t melody) {
  ringCommon(alarmIdx, label, melody, AT_ALARM);
  app.mode = UI_RINGING;
  audioPlay(melody < MEL_FIRST_NOTIFY ? (MelodyId)melody : MEL_CLASSIC, true);
  Serial.printf("[ALARM] ringing: %s (melody %d)\n", app.ringLabel, (int)melody);
}

void appStartNotify(int alarmIdx, const char* label, uint8_t melody) {
  ringCommon(alarmIdx, label, melody, AT_REMINDER);
  app.mode = UI_NOTIFY;
  app.notifyAt = millis();
  audioPlayNotify(melody >= MEL_FIRST_NOTIFY ? (MelodyId)melody : MEL_NOTIFY);
  Serial.printf("[REMINDER] notify: %s\n", app.ringLabel);
}

static uint32_t alarmSig(const Alarm& a) {
  return ((uint32_t)a.hour << 16) | ((uint32_t)a.minute << 8) | a.kind;
}

static void stopRinging(bool snooze, bool automatic) {
  if (!app.ringing) return;
  audioStop();
  int idx = app.ringingIdx;
  uint8_t type = app.ringType;
  app.ringing = false;
  app.ringingIdx = -1;
  app.mode = app.returnMode == UI_WIDGET ? UI_WIDGET : UI_FACE;
  app.face = snooze ? EX_SLEEPY : appHomeFace();
  app.emoKick = true;
  if (!automatic) app.ringAuto = 0;                // a person answered: the auto-snooze count starts over
  if (snooze) {
    alarmSnooze(SNOOZE_MINUTES, app.ringLabel, app.ringMelody, type);
    Serial.printf("[ALARM] snoozed %d min%s\n", SNOOZE_MINUTES, automatic ? " (nobody answered)" : "");
  } else {
    if (idx >= 0) {
      Alarm a = alarmGet(idx);
      // only switch off the entry that actually rang (the list may have been edited meanwhile)
      if ((a.kind == AK_ONCE || a.kind == AK_COUNTDOWN) && alarmSig(a) == app.ringSig) {
        a.enabled = 0;
        alarmUpdate(idx, a);
      }
    }
    snoozeClear();
    Serial.println(automatic ? "[ALARM] gave up (nobody answered)" : "[ALARM] dismissed");
  }
  sfx(SFX_OK);
}

void appStopRinging(bool snooze) { stopRinging(snooze, false); }

// an alarm nobody answers does not scream forever: it snoozes, and after
// RING_AUTO_SNOOZES rounds it gives up quietly
static void ringTimeoutTick(uint32_t now) {
  if (!app.ringing) return;
  uint32_t lim = (app.ringType == AT_REMINDER) ? RING_REMINDER_MS : RING_ALARM_MS;
  if (now - app.ringStart < lim) return;
  if (app.ringAuto < RING_AUTO_SNOOZES) { app.ringAuto++; stopRinging(true, true); }
  else { app.ringAuto = 0; stopRinging(false, true); }
}

// pomodoro events: 1 focus done, 2 break done, 3 skipped
void appPomoEvent(uint8_t evt) {
  if (evt == 1) {
    react(EX_PARTY, 5000);
    audioPlaySfx(SFX_FANFARE);                 // timer sounds are never muted
  } else if (evt == 2) {
    react(EX_WOW, 3000);
    audioPlaySfx(SFX_OK);
  } else {
    sfx(SFX_CLICK);
  }
}

bool appAction(const char* a) {
  if (!strcmp(a, "pet")) { appWake(); app.dndIdle = millis(); appPet(); return true; }
  if (!strcmp(a, "party")) { appWake(); app.dndIdle = millis(); appParty(); return true; }
  if (!strcmp(a, "sleep")) { appSleepNow(); return true; }
  if (!strcmp(a, "wake")) { appWake(); return true; }
  if (!strcmp(a, "widgets") || !strcmp(a, "clock")) {
    appWake();
    if (app.mode == UI_FACE) { app.mode = UI_WIDGET; app.widget = widgetNext(WG_COUNT - 1, settings.widgets); }
    return true;
  }
  if (!strcmp(a, "next")) { if (app.mode == UI_WIDGET) app.widget = widgetNext(app.widget, settings.widgets); return true; }
  if (!strcmp(a, "weather")) { weatherRefresh(); return true; }
  if (!strcmp(a, "face")) { appWake(); if (app.mode == UI_WIDGET) app.mode = UI_FACE; return true; }
  return false;
}

// ============================================================
//  time service: alarms, reminders and the hourly cuckoo
// ============================================================

static int lastMinKey = -1;

static void cuckooCheck(const struct tm& t) {
  if (!settings.cuckoo || t.tm_min != 0) return;
  if (t.tm_hour < CUCKOO_FIRST_HOUR || t.tm_hour > CUCKOO_LAST_HOUR) return;
  if (dndActiveNow()) return;             // no chimes while do-not-disturb is on
  if (app.ringing || settings.volume == 0) return;
  int n = t.tm_hour % 12;
  if (n == 0) n = 12;
  audioCuckoo((uint8_t)n);
  react(EX_WOW, (uint32_t)n * 900UL + 600UL);
  rigLookAt(app.rig, 0.0f, -0.9f);              // looks up at the little bird
  Serial.printf("[CUCKOO] %d\n", n);
}

static void fireEntry(int idx, const Alarm& a) {
  app.ringSig = alarmSig(a);
  app.ringAuto = 0;                                  // a fresh alarm: auto-snooze count starts over
  if (a.type == AT_REMINDER) appStartNotify(idx, a.label, a.melody);
  else appStartRinging(idx, a.label, a.melody);
}

// Alarms that come due while another one is ringing (or in the same minute) wait in
// line instead of being lost. A copy of the entry is kept, so editing the list is safe.
struct PendingAlarm { Alarm a; int idx; time_t due; };
static PendingAlarm pend[8];
static uint8_t nPend = 0;

static void queueEntry(int idx, const Alarm& a) {
  if (nPend >= 8) return;
  time_t now = time(nullptr);
  for (uint8_t i = 0; i < nPend; i++)                // never queue the same entry twice
    if (pend[i].idx == idx && alarmSig(pend[i].a) == alarmSig(a)) return;
  pend[nPend].a = a; pend[nPend].idx = idx; pend[nPend].due = now;
  nPend++;
}

static void firePending() {
  if (app.ringing) return;
  time_t now = time(nullptr);
  while (nPend) {
    PendingAlarm p = pend[0];
    for (uint8_t i = 1; i < nPend; i++) pend[i - 1] = pend[i];
    nPend--;
    if (now - p.due > 30 * 60) continue;             // far too late by now: drop it
    fireEntry(p.idx, p.a);
    return;
  }
}

// power cut / reboot: one-time alarms that came due while the buddy was off still ring
// when the clock is back, if they are at most MISSED_ALARM_MS late
// (an entry only switches itself off when dismissed, so a finished one never repeats)
static void catchUpMissed(time_t now) {
  for (int i = 0; i < alarmsCount(); i++) {
    Alarm a = alarmGet(i);
    time_t ts;
    if (!a.enabled || !alarmTargetTime(a, ts)) continue;
    long late = (long)(now - ts);
    if (late >= 60 && late <= (long)(MISSED_ALARM_MS / 1000UL)) {
      queueEntry(i, a);
      Serial.printf("[ALARM] '%s' came due while the buddy was off - ringing now\n", a.label);
    }
  }
}

static void timeService() {
  if (!clockOk()) return;
  time_t now = time(nullptr);
  struct tm tmv; localtime_r(&now, &tmv);
  int key = tmv.tm_hour * 60 + tmv.tm_min;
  if (key != lastMinKey) {
    bool first = (lastMinKey < 0);
    lastMinKey = key;
    uint8_t before = nPend;
    for (int i = alarmsCheck(tmv, 0); i >= 0; i = alarmsCheck(tmv, i + 1))
      queueEntry(i, alarmGet(i));
    if (first) catchUpMissed(now);
    // the hourly cuckoo only chimes when no alarm is due (and never just because we booted at xx:00)
    else if (nPend == before && !app.ringing) cuckooCheck(tmv);
  }
  firePending();
  if (!app.ringing && snoozeActive() && snoozeFired()) {
    if (snoozeType() == AT_REMINDER) appStartNotify(-1, snoozeLabel(), snoozeMelody());
    else appStartRinging(-1, snoozeLabel(), snoozeMelody());
    snoozeClear();
  }
}

// ============================================================
//  personality: auto faces, gaze, blinks, scenes
// ============================================================

static Expression pickAutoFace() {
  const MoodDef& m = M();
  bool night = false;
  if (clockOk() && curMood() != MD_ACTIVE) {
    time_t t = time(nullptr);
    struct tm tmv; localtime_r(&t, &tmv);
    night = (tmv.tm_hour >= QUIET_START_HOUR || tmv.tm_hour < QUIET_END_HOUR);
  }
  if (night && (rand32() & 1)) return (rand32() & 1) ? EX_SLEEPY : EX_SLEEP;
  Expression e = (Expression)m.pool[rand32() % m.poolN];
  for (int tries = 0; tries < 4 && e == app.face; tries++)
    e = (Expression)m.pool[rand32() % m.poolN];
  return e;
}

static void gazeTick(uint8_t flags) {
  const MoodDef& m = M();
  uint32_t now = millis();
  int tilt = sensorsTilt();
  float sd = (float)app.sideSign;
  if (app.petting)               rigLookAt(app.rig, 0.0f, -0.6f);      // looks up toward the hand
  else if (app.mode == UI_NOTIFY) rigLookAt(app.rig, 0.0f, -1.0f);     // looks up at the banner
  else if (flags & F_LOOK_SIDE)  rigLookAt(app.rig, sd * 0.95f, 0.05f);
  else if (flags & F_LOOK_UP)    rigLookAt(app.rig, sd * 0.65f, -0.85f);
  else if (flags & F_AVERT)      rigLookAt(app.rig, -sd * 0.7f, 0.7f);
  else if (tilt != 0)            rigLookAt(app.rig, (float)tilt, app.rig.lookTY);
  else if (now > app.nextLook) {
    app.nextLook = now + rrange(m.lookMin, m.lookMax);
    rigLookAt(app.rig, frand(-1.0f, 1.0f) * m.lookAmp, frand(-0.6f, 0.6f) * m.lookAmp);
  }
}

static void blinkTick() {
  const MoodDef& m = M();
  uint32_t now = millis();
  if (now > app.nextBlink) {
    app.rig.blinkTarget = 1;
    // sometimes a quick double blink
    app.nextBlink = (rand32() % 100 < 22) ? now + 280 : now + rrange(m.blinkMin, m.blinkMax);
  }
}

static void startScene() {
  const MoodDef& m = M();
  if (!m.nScenes) return;
  int i = rand32() % m.nScenes;
  app.script = m.scenes[i];
  app.scriptLen = m.sceneLen[i];
  app.scriptIdx = 0;
  app.scriptNext = 0;               // play first step immediately
}

static void faceModeTick() {
  const MoodDef& m = M();
  uint32_t now = millis();

  // pomodoro owns the face while it runs
  PomoState ps = pomoState();
  if (ps != app.lastPomo) {
    app.lastPomo = ps;
    app.script = nullptr;
    if (ps == PO_FOCUS) app.face = EX_FOCUS;
    else if (ps == PO_BREAK) app.face = EX_CHILL;
    else app.face = appHomeFace();
  }

  // scripted scene playback
  if (app.script) {
    if (now >= app.scriptNext) {
      if (app.scriptIdx < app.scriptLen) {
        const Scene& st = app.script[app.scriptIdx++];
        app.face = (Expression)st.e;
        app.scriptNext = now + st.ms;
      } else {
        app.script = nullptr;
        app.face = appHomeFace();
        app.nextAutoFace = now + rrange(m.chgMin, m.chgMax);
        app.nextScene = now + rrange(m.sceneMin, m.sceneMax);
      }
    }
  } else if (settings.autoFace && ps == PO_IDLE && now > app.reactUntil && app.face != EX_SLEEP) {
    if (m.nScenes && m.sceneMax && now > app.nextScene) {
      startScene();
    } else if (now > app.nextAutoFace) {
      app.nextAutoFace = now + rrange(m.chgMin, m.chgMax);
      app.face = pickAutoFace();
    }
  }
}

// ============================================================
//  drawing
// ============================================================

static void textCenter(U8G2& g, int y, const char* s, int cw) {
  int w = (int)strlen(s) * cw;
  int x = 64 - w / 2;
  if (x < 0) x = 0;                                  // u8g2 cannot draw at negative x (it wraps around)
  g.drawStr(x, y, s);
}

static void drawFaceScreen(U8G2& g) {
  drawFace(g, app.rig, curPack(), FACE_CY);

  PomodoroStatus s = pomoStatus();
  if (s.state != PO_IDLE) {
    g.setFont(u8g2_font_5x7_tr);
    char b[24];
    unsigned mm = s.remaining / 60, ss = s.remaining % 60;
    if (!s.paused || ((millis() / 500) & 1)) {
      snprintf(b, sizeof(b), "%s %02u:%02u", s.state == PO_FOCUS ? "STUDY" : "BREAK", mm, ss);
      g.drawStr(2, 7, b);
    }
    if (s.total) {
      int w = (int)((126UL * (s.total - s.remaining)) / s.total);
      g.drawHLine(1, 62, 126);
      g.drawBox(1, 61, w, 3);
    }
  } else if (snoozeActive()) {
    g.setFont(u8g2_font_5x7_tr);
    char b[32];
    unsigned long r = (unsigned long)snoozeRemaining();
    snprintf(b, sizeof(b), "zz %lu:%02lu", r / 60, r % 60);
    g.drawStr(128 - (int)strlen(b) * 5 - 2, 7, b);
  }
}

// widget mode: little eyes stay on top of every screen, the widget fills the rest
static void drawWidgetMode(U8G2& g) {
  drawFace(g, app.rig, curPack(), 13, 0.5f);
  widgetDraw(g, app.widget);
  PomodoroStatus s = pomoStatus();
  if (s.state != PO_IDLE) {                           // study / break countdown in the top-left corner
    g.setFont(u8g2_font_5x7_tr);
    char b[24];
    snprintf(b, sizeof(b), "%s %u:%02u", s.state == PO_FOCUS ? "STUDY" : "BREAK", (unsigned)(s.remaining / 60), (unsigned)(s.remaining % 60));
    g.drawStr(2, 7, b);
  }
  // little page dots, top-right: which widget am I on
  int n = 0, pos = 0;
  for (int i = 0; i < WG_COUNT; i++) if (settings.widgets & (1 << i)) { if (i == app.widget) pos = n; n++; }
  for (int i = 0; i < n && n > 1; i++) {
    int x = 125 - (n - 1 - i) * 5;
    if (i == pos) gfxEllipse(g, x, 4, 1, 1, 1); else gfxPixel(g, x, 4, 1);
  }
}

static void drawRinging(U8G2& g) {
  drawFace(g, app.rig, curPack(), 34);
  g.setFont(u8g2_font_6x12_tr);
  String t = clockTimeStr();
  textCenter(g, 10, t.c_str(), 6);
  g.setFont(u8g2_font_5x7_tr);
  if (app.ringLabel[0]) {
    char lb[26];
    snprintf(lb, sizeof(lb), "%.25s", app.ringLabel);              // 25 chars x 5 px fits the 128 px screen
    textCenter(g, 63, lb, 5);
  } else textCenter(g, 63, "tap = off   hold = snooze", 5);
  if ((millis() / 350) & 1) {           // inverted flash
    g.setDrawColor(2);
    g.drawBox(0, 0, 128, 64);
    g.setDrawColor(1);
  }
}

// split text into two lines of at most `w` characters, breaking on a space when possible
static void wrap2(const char* s, int w, char* l1, char* l2) {
  int n = (int)strlen(s);
  l1[0] = l2[0] = 0;
  if (n <= w) { strcpy(l1, s); return; }
  int cut = w;
  for (int i = w; i > w / 2; i--) if (s[i] == ' ') { cut = i; break; }
  memcpy(l1, s, cut); l1[cut] = 0;
  const char* rest = s + cut;
  while (*rest == ' ') rest++;
  strncpy(l2, rest, w); l2[w] = 0;
}

// phone-style notification: drops in from the top edge, buzzes, bounces to rest
static void drawNotify(U8G2& g) {
  uint32_t t = millis() - app.notifyAt;
  float k = t >= 380 ? 1.0f : t / 380.0f;
  float c1 = 1.70158f, c3 = c1 + 1.0f, km = k - 1.0f;
  float e = 1.0f + c3 * km * km * km + c1 * km * km;           // ease-out-back
  int by = (int)lroundf(-38.0f + 40.0f * e);
  int sh = (t < 700) ? (((t / 40) & 1) ? 1 : -1) : 0;           // the "buzz"

  drawFace(g, app.rig, curPack(), 50, 0.55f);

  gfxRBox(g, 2 + sh, by, 124, 36, 6, 1);                        // the card (clipped: slides in from above the edge)
  // bell icon
  gfxRBox(g, 7 + sh, by + 6, 9, 8, 3, 0);
  gfxHLine(g, 5 + sh, by + 14, 13, 0);
  gfxHLine(g, 11 + sh, by + 16, 1, 0);
  gfxHLine(g, 7 + sh, by + 16, 114, 0);                         // divider under the title row
  g.setDrawColor(0);
  // text is only drawn once it is fully on screen (u8g2 cannot draw at negative y)
  g.setFont(u8g2_font_5x7_tr);
  if (by + 12 >= 8) {
    g.drawStr(21 + sh, by + 12, "REMINDER");
    String ts = clockTimeStr();
    g.drawStr(122 + sh - (int)ts.length() * 5, by + 12, ts.c_str());
  }
  char l1[24], l2[24];
  wrap2(app.ringLabel[0] ? app.ringLabel : "Reminder", 22, l1, l2);
  g.setFont(u8g2_font_5x8_tr);
  if (by + 26 >= 9) g.drawStr(7 + sh, by + 26, l1);
  if (l2[0] && by + 34 >= 9) g.drawStr(7 + sh, by + 34, l2);
  g.setDrawColor(1);
}

enum MenuIdx : uint8_t {
  MI_FACES = 0, MI_STYLE, MI_MOOD, MI_TIMER, MI_AGENDA, MI_TASKS, MI_WIDGETS, MI_SOUND,
  MI_CUCKOO, MI_CLOCKFMT, MI_BRIGHT, MI_SLEEP, MI_INFO, MI_SENS, MI_EXIT, MI_COUNT
};

static void drawBrowser(U8G2& g) {
  drawFace(g, app.rig, curPack(), 24);
  g.setFont(u8g2_font_6x10_tr);
  char b[40];
  const char* title; const char* sub = nullptr; int n, idx;
  if (app.menuSub == 1) {
    title = faceName(app.browseFace); idx = app.browseFace; n = EX_COUNT;
  } else if (app.menuSub == 2) {
    title = PACK_NAMES[curPack()]; sub = PACK_DESCS[curPack()]; idx = curPack(); n = PK_COUNT;
  } else {
    title = M().name; sub = M().desc; idx = curMood(); n = MD_COUNT;
  }
  snprintf(b, sizeof(b), "%s", title);
  textCenter(g, sub ? 54 : 58, b, 6);
  g.setFont(u8g2_font_5x7_tr);
  if (sub) {
    char d[30]; snprintf(d, sizeof(d), "%.25s", sub);
    textCenter(g, 63, d, 5);
  } else {
    textCenter(g, 63, "tap = next   hold = pick", 5);
  }
  snprintf(b, sizeof(b), "%d/%d", idx + 1, n);
  g.drawStr(128 - (int)strlen(b) * 5 - 2, 7, b);
}

static void drawMenu(U8G2& g) {
  static const char* items[MI_COUNT] = {
    "Faces", "Eye style", "Mood", "Study timer", "Up next", "Tasks", "Widgets", "Sound",
    "Cuckoo", "Clock", "Bright", "Sleep", "Info", "Sensors", "Exit"
  };
  g.setFont(u8g2_font_6x10_tr);
  g.drawBox(0, 0, 128, 11);
  g.setDrawColor(0);
  g.drawStr(4, 9, "MENU");
  char b[24];
  snprintf(b, sizeof(b), "%d/%d", app.menuItem + 1, (int)MI_COUNT);
  g.drawStr(124 - (int)strlen(b) * 6, 9, b);
  g.setDrawColor(1);

  const int visible = 4;
  int start = app.menuItem <= 1 ? 0 : app.menuItem - 1;
  if (start + visible > MI_COUNT) start = MI_COUNT - visible;
  for (int i = 0; i < visible; i++) {
    int idx = start + i;
    int y = 13 + i * 12;
    bool sel = (idx == app.menuItem);
    if (sel) { g.drawRBox(1, y, 126, 11, 2); g.setDrawColor(0); }
    g.drawStr(6, y + 9, items[idx]);
    const char* val = nullptr;
    char vb[16] = "";
    switch (idx) {
      case MI_FACES:  val = faceName(app.face); break;
      case MI_STYLE:  val = PACK_NAMES[curPack()]; break;
      case MI_MOOD:   val = M().name; break;
      case MI_TIMER:
        if (pomoRunning()) {
          PomodoroStatus s = pomoStatus();
          snprintf(vb, sizeof(vb), "%um", (unsigned)(s.remaining / 60 + 1));
          val = vb;
        } else val = "start";
        break;
      case MI_TASKS: {
        if (clockOk()) {
          time_t t = time(nullptr);
          struct tm td; localtime_r(&t, &td);
          int tt = 0, dd = 0;
          tasksCountDay(td, tt, dd);
          if (tt) { snprintf(vb, sizeof(vb), "%d/%d", dd, tt); val = vb; }
          else val = "none";
        } else val = "-";
        break;
      }
      case MI_SOUND:
        if (settings.volume == 0) val = "off";
        else { snprintf(vb, sizeof(vb), "%u", (unsigned)settings.volume); val = vb; }
        break;
      case MI_CUCKOO: val = settings.cuckoo ? "on" : "off"; break;
      case MI_CLOCKFMT: val = settings.hour12 ? "12 hour" : "24 hour"; break;
      case MI_BRIGHT:
        snprintf(vb, sizeof(vb), "%u%%", (unsigned)((settings.brightness * 100UL) / 255));
        val = vb; break;
      default: break;
    }
    if (val) g.drawStr(122 - (int)strlen(val) * 6, y + 9, val);
    g.setDrawColor(1);
  }
}

// ---- agenda: everything scheduled, soonest first ----
struct AgendaItem { int idx; time_t ts; };
static int buildAgenda(AgendaItem* out, int cap) {
  int n = 0;
  for (int i = 0; i < alarmsCount() && n < cap; i++) {
    time_t ts;
    if (alarmGet(i).enabled && alarmNextFire(i, ts)) { out[n].idx = i; out[n].ts = ts; n++; }
  }
  for (int a = 1; a < n; a++) {                      // insertion sort by time
    AgendaItem k = out[a]; int b = a - 1;
    while (b >= 0 && out[b].ts > k.ts) { out[b + 1] = out[b]; b--; }
    out[b + 1] = k;
  }
  return n;
}

static void drawAgenda(U8G2& g) {
  static AgendaItem items[MAX_ALARMS];
  int n = buildAgenda(items, MAX_ALARMS);
  g.setFont(u8g2_font_6x10_tr);
  g.drawBox(0, 0, 128, 11);
  g.setDrawColor(0);
  g.drawStr(4, 9, "UP NEXT");
  char b[48];
  snprintf(b, sizeof(b), "%d", n);
  g.drawStr(124 - (int)strlen(b) * 6, 9, b);
  g.setDrawColor(1);
  if (n == 0) {
    g.setFont(u8g2_font_6x10_tr);
    textCenter(g, 38, "nothing scheduled", 6);
    g.setFont(u8g2_font_5x7_tr);
    textCenter(g, 63, "add some in the web app", 5);
    return;
  }
  if (app.agendaTop >= n) app.agendaTop = 0;
  time_t nowT = time(nullptr);
  struct tm today; localtime_r(&nowT, &today);
  g.setFont(u8g2_font_5x8_tr);
  for (int r = 0; r < 4 && app.agendaTop + r < n; r++) {
    const AgendaItem& it = items[app.agendaTop + r];
    Alarm a = alarmGet(it.idx);
    struct tm tmv; localtime_r(&it.ts, &tmv);
    char when[24];
    {
      String ft = clockFormat(tmv);
      if (tmv.tm_yday == today.tm_yday && tmv.tm_year == today.tm_year) snprintf(when, sizeof(when), "%s", ft.c_str());
      else { char wd[6]; strftime(wd, sizeof(wd), "%a", &tmv); snprintf(when, sizeof(when), "%s %s", wd, ft.c_str()); }
    }
    char line[64];
    int room = 24 - (int)strlen(when) - 1;                          // ~24 chars fit next to the icon
    snprintf(line, sizeof(line), "%s %.*s", when, room, a.label[0] ? a.label : (a.type == AT_REMINDER ? "Reminder" : "Alarm"));
    int y = 13 + r * 12;
    g.drawRFrame(1, y, 9, 10, 2);
    g.drawStr(3, y + 8, a.type == AT_REMINDER ? "R" : "A");
    g.drawStr(14, y + 8, line);
  }
  g.setFont(u8g2_font_5x7_tr);
  if (n > 4) {
    snprintf(b, sizeof(b), "%d-%d of %d", app.agendaTop + 1, (app.agendaTop + 4 > n) ? n : app.agendaTop + 4, n);
    g.drawStr(2, 63, b);
  }
  g.drawStr(128 - 5 * 11, 63, "hold=back");
}

// ---- tasks: one day at a time, tap = next day ----
#define TASK_BROWSE_DAYS 31

static void drawTasksDay(U8G2& g) {
  if (!clockOk()) {
    g.setFont(u8g2_font_6x10_tr);
    textCenter(g, 32, "waiting for time", 6);
    g.setFont(u8g2_font_5x7_tr);
    textCenter(g, 63, "hold = back", 5);
    return;
  }
  time_t now = time(nullptr);
  struct tm base; localtime_r(&now, &base);
  base.tm_hour = 12; base.tm_min = 0; base.tm_sec = 0;   // noon keeps +N days clear of DST edges
  time_t d = mktime(&base) + (time_t)app.taskOff * 86400;
  struct tm day; localtime_r(&d, &day);
  int total = 0, done = 0;
  tasksCountDay(day, total, done);

  g.setFont(u8g2_font_6x10_tr);
  g.drawBox(0, 0, 128, 11);
  g.setDrawColor(0);
  g.drawStr(4, 9, "TASKS");
  {
    char b[24];
    if (app.taskOff == 0) snprintf(b, sizeof(b), "today %d/%d", done, total);
    else {
      char db[16];
      strftime(db, sizeof(db), "%a %d", &day);
      snprintf(b, sizeof(b), "%s %d/%d", db, done, total);
    }
    g.drawStr(124 - g.getStrWidth(b), 9, b);
  }
  g.setDrawColor(1);
  if (!total) {
    g.setFont(u8g2_font_6x10_tr);
    textCenter(g, 38, "no tasks this day", 6);
    g.setFont(u8g2_font_5x7_tr);
    textCenter(g, 63, "tap=next day  hold=back", 5);
    return;
  }

  static int idx[MAX_TASKS];
  int n = tasksForDay(day, idx, MAX_TASKS);
  int rows = n < 4 ? n : 4;
  for (int r = 0; r < rows; r++) {
    Task t = taskGet(idx[r]);
    int y = 13 + r * 12;                     // rows at y = 13, 25, 37, 49
    if (t.done) {                            // filled checkbox with knocked-out tick
      g.drawRBox(2, y, 10, 10, 2);
      g.setDrawColor(0);
      g.drawLine(4, y + 5, 6, y + 7);
      g.drawLine(6, y + 7, 10, y + 2);
      g.setDrawColor(1);
    } else {
      g.drawRFrame(2, y, 10, 10, 2);
    }
    int x = 16;
    g.setFont(u8g2_font_5x8_tr);
    if (t.hour != TASK_NO_TIME) {             // "9:05a" / "21:05"
      struct tm tt = {};
      tt.tm_hour = t.hour; tt.tm_min = t.minute;
      char hm[8], ap[4], ts[10];
      clockParts(tt, hm, sizeof(hm), ap, sizeof(ap));
      if (ap[0]) snprintf(ts, sizeof(ts), "%s%c ", hm, ap[0] == 'A' ? 'a' : 'p');
      else snprintf(ts, sizeof(ts), "%s ", hm);
      g.drawStr(x, y + 8, ts);
      x += g.getStrWidth(ts);
    }
    // a span shows its last day on its first day: "trip >9"
    char tail[8] = "";
    if (t.hasEnd && taskStartsOn(t, day)) snprintf(tail, sizeof(tail), ">%u", (unsigned)t.endDay);
    int room = (126 - x) / 5 - (int)strlen(tail);
    char line[TASK_TITLE_LEN + 8];
    snprintf(line, sizeof(line), "%.*s%s", room > 0 ? room : 0, t.title, tail);
    g.drawStr(x, y + 8, line);
    if (t.done) g.drawHLine(x - 1, y + 5, g.getStrWidth(line) + 1);   // strike-through
  }
  g.setFont(u8g2_font_5x7_tr);
  if (n > 4) {
    char b[12];
    snprintf(b, sizeof(b), "+%d more", n - 4);
    g.drawStr(2, 63, b);
  }
  g.drawStr(128 - g.getStrWidth("tap=+1d hold=back"), 63, "tap=+1d hold=back");
}

static void drawInfoScreen(U8G2& g) {
  g.setFont(u8g2_font_5x8_tr);
  g.drawStr(2, 8, "Desk Buddy v" BUDDY_VERSION);
  char b[40];
  bool sta = WiFi.status() == WL_CONNECTED;
  if (sta) snprintf(b, sizeof(b), "http://%s", WiFi.localIP().toString().c_str());
  else snprintf(b, sizeof(b), "WiFi: not connected");
  g.drawStr(2, 19, b);
  if (clockSynced()) snprintf(b, sizeof(b), "Time: %s", clockServerInUse());
  else snprintf(b, sizeof(b), "Time: waiting for sync");
  g.drawStr(2, 29, b);
  snprintf(b, sizeof(b), "Heap %u kB   up %lum", (unsigned)(ESP.getFreeHeap() / 1024),
           (unsigned long)(millis() / 60000UL));
  g.drawStr(2, 39, b);
  snprintf(b, sizeof(b), "%s / %s", PACK_NAMES[curPack()], M().name);
  g.drawStr(2, 49, b);
  g.drawStr(2, 63, "hold = back");
}

static void drawSensorScreen(U8G2& g) {
  g.setFont(u8g2_font_5x8_tr);
  g.drawStr(2, 8, "Sensor test (live)");
  char b[36];
  snprintf(b, sizeof(b), "TOUCH pin %u: %s", (unsigned)sensorsTouchPin(),
           sensorsTouched() ? "TOUCHED!" : "-------");
  g.setFont(u8g2_font_6x12_tr);
  if (sensorsTouched()) { g.drawBox(0, 13, 128, 14); g.setDrawColor(0); }
  g.drawStr(2, 24, b);
  g.setDrawColor(1);
  g.setFont(u8g2_font_5x8_tr);
  snprintf(b, sizeof(b), "mode:%s taps:%u", sensorsLatchMode() ? "toggle" : "momentary",
           (unsigned)sensorsTapCount());
  g.drawStr(2, 38, b);
  snprintf(b, sizeof(b), "TILT pin %u lvl %d shk %u", (unsigned)sensorsTiltPin(),
           sensorsTilt(), (unsigned)sensorsShakeCount());
  g.drawStr(2, 48, b);
  snprintf(b, sizeof(b), "tipped %lu.%lus", (unsigned long)(sensorsTiltHeldMs() / 1000UL),
           (unsigned long)((sensorsTiltHeldMs() / 100UL) % 10UL));
  g.drawStr(2, 58, b);
  g.drawStr(78, 63, "hold=back");
}

// ============================================================
//  menu logic
// ============================================================

static void leaveMenu() {
  app.menuItem = 0; app.menuSub = 0; app.subView = 0;
  app.mode = app.returnMode == UI_WIDGET ? UI_WIDGET : UI_FACE;
}

static void menuTick(TouchEvt te) {
  switch (te) {
    case TE_TAP:
    case TE_DOUBLE:
      if (app.menuSub == 1) {
        app.browseFace = (Expression)((app.browseFace + 1) % EX_COUNT);
      } else if (app.menuSub == 2) {
        settings.pack = (settings.pack + 1) % PK_COUNT;
        sfx(SFX_CLICK);
      } else if (app.menuSub == 3) {
        settings.mood = (settings.mood + 1) % MD_COUNT;
        sfx(SFX_CLICK);
      } else if (app.subView == 4) {
        app.agendaTop += 4;                         // next page of the agenda
        sfx(SFX_CLICK);
      } else if (app.subView == 5) {
        app.taskOff = (app.taskOff >= TASK_BROWSE_DAYS) ? 0 : app.taskOff + 1;   // next day
        sfx(SFX_CLICK);
      } else if (app.subView == 0) {
        app.menuItem = (app.menuItem + 1) % MI_COUNT;
        sfx(SFX_CLICK);
      }
      break;
    case TE_HOLD:
      if (app.menuSub) {
        if (app.menuSub == 1) { app.face = app.browseFace; app.script = nullptr; }
        if (app.menuSub == 3) appStyleChanged();
        if (app.menuSub >= 2) settingsSave();
        app.menuSub = 0;
        sfx(SFX_OK);
      } else if (app.subView) {
        app.subView = 0;
      } else switch (app.menuItem) {
        case MI_FACES: app.menuSub = 1; app.browseFace = app.face; break;
        case MI_STYLE: app.menuSub = 2; break;
        case MI_MOOD:  app.menuSub = 3; break;
        case MI_TIMER:
          if (pomoRunning()) pomoReset(); else pomoStart();
          sfx(SFX_OK);
          leaveMenu();
          app.mode = UI_FACE;
          break;
        case MI_AGENDA: app.subView = 4; app.agendaTop = 0; break;
        case MI_TASKS:  app.subView = 5; app.taskOff = 0; break;
        case MI_WIDGETS:
          app.menuItem = 0; app.menuSub = 0; app.mode = UI_WIDGET;
          app.widget = widgetNext(WG_COUNT - 1, settings.widgets);
          sfx(SFX_OK);
          break;
        case MI_SOUND: {
          static const uint8_t lv[] = { 0, 3, 6, 10 };
          uint8_t next = 0;
          for (int i = 0; i < 4; i++) if (settings.volume < lv[i]) { next = lv[i]; break; }
          if (settings.volume >= 10) next = 0;
          settings.volume = next;
          audioSetVolume(next);
          settingsSave();
          audioPlaySfx(SFX_OK);
          break;
        }
        case MI_CLOCKFMT:
          settings.hour12 = !settings.hour12;
          settingsSave();
          sfx(SFX_OK);
          break;
        case MI_CUCKOO:
          settings.cuckoo = !settings.cuckoo;
          settingsSave();
          if (settings.cuckoo) audioCuckoo(1);
          break;
        case MI_BRIGHT: {
          int nb = (settings.brightness >= 230) ? 60 : settings.brightness + 60;
          settings.brightness = (uint8_t)(nb > 255 ? 255 : nb);
          displaySetBrightness(settings.brightness);
          settingsSave();
          break;
        }
        case MI_SLEEP: appSleepNow(); break;
        case MI_INFO:  app.subView = 1; break;
        case MI_SENS:  app.subView = 2; break;
        case MI_EXIT:  leaveMenu(); break;
      }
      break;
    default: break;
  }
}

static void ringingTick(TouchEvt te) {
  if (app.mode == UI_RINGING) app.rig.wobble = 1.0f;
  switch (te) {
    case TE_TAP:  appStopRinging(false); break;   // one tap = stop
    case TE_HOLD: appStopRinging(true); break;    // hold = remind me again in a few minutes
    default: break;
  }
}

// ============================================================
//  main tick
// ============================================================

// ============================================================
//  petting (scratching the head / stroking) + gestures
// ============================================================

static void beginPetting(uint32_t startMs) {
  app.petting = true;
  app.petStart = startMs;
  app.petPhase = 255;
  app.nextMurmur = startMs + 1500;      // first soft mumble after a moment of trust
  app.script = nullptr;
  app.rig.leanT = 1.0f;                               // head leans into the hand
}

static void endPetting(bool quiet) {
  if (!app.petting) return;
  uint32_t dur = millis() - app.petStart;
  app.petting = false;
  app.rig.leanT = 0.0f;
  audioStopPurr();
  if (!quiet) {                                       // afterglow: a pleased face + a happy sound
    react((Expression)M().afterglow, dur < 1500 ? 1600 : 2800, true);
    rigImpulse(app.rig, -1.2f * M().bounce);
  }
}

// while the hand stays: relief -> deeper bliss -> love, with a soft murmur
// every few seconds - a contented pet, not a continuous buzzer
static void pettingTick(uint32_t now) {
  uint32_t el = now - app.petStart;
  uint8_t ph = el < 1800 ? 0 : (el < 3600 ? 1 : 2);
  if (ph != app.petPhase) {
    app.petPhase = ph;
    if (ph == 2 && soundOk()) audioPlaySfx(SFX_COO);  // the "I love this" moment
  }
  app.reactFace = (Expression)M().petPhase[ph];       // held while the hand is there (the murmur is the sound)
  app.reactUntil = now + 500;
  if (soundOk() && !audioBusy() && (int32_t)(now - app.nextMurmur) >= 0) {
    audioPurr();                                      // one mumble, then quiet again
    app.nextMurmur = now + 3500 + (rand32() % 3000);  // next one in 3.5 - 6.5 s
  }
}

// ============================================================
//  tilt switch: picked up / tipped / rocked / set back down
//  (a single ball switch gives "tipped or not", for how long, and how often it
//   flips - that is plenty to read as a mood)
// ============================================================

static_assert(MD_COUNT == 7, "tilt tables below need one row per mood");

// faces while it is held tipped:  curious -> uneasy -> upset -> crying   (one row per Mood)
static const uint8_t TILT_FACES[MD_COUNT][4] = {
  /* balanced */ { EX_CONFUSED, EX_DIZZY,   EX_SAD,   EX_CRY    },
  /* dramatic */ { EX_SURPRISED, EX_PANIC,  EX_BAWL,  EX_CRY    },
  /* silent   */ { EX_CONFUSED, EX_NERVOUS, EX_SAD,   EX_SAD    },
  /* active   */ { EX_WOW,      EX_GIGGLE,  EX_DIZZY, EX_SAD    },
  /* romantic */ { EX_SHY,      EX_NERVOUS, EX_SAD,   EX_CRY    },
  /* grumpy   */ { EX_SUS,      EX_ANGRY,   EX_POUT,  EX_ANGRY  },
  /* sleepy   */ { EX_CONFUSED, EX_DIZZY,   EX_BORED, EX_SLEEPY },
};
// gently rocked (like a cradle)
static const uint8_t TILT_ROCK[MD_COUNT]   = { EX_GIGGLE, EX_LAUGH, EX_GIGGLE, EX_XD, EX_DREAMY, EX_POUT, EX_SLEEPY };
// set back down after being held tipped for a while: "phew"
static const uint8_t TILT_RELIEF[MD_COUNT] = { EX_HAPPY, EX_HAPPY, EX_HAPPY, EX_HAPPY, EX_SHY, EX_SMUG, EX_CHILL };

static void tiltTick(uint32_t now) {
  uint32_t held = sensorsTiltHeldMs();
  uint32_t back = held ? 0 : sensorsTiltReturned();   // (only consumed once it is really upright again)
  uint8_t  rock = sensorsTiltRock();

  bool free = (app.mode == UI_FACE || app.mode == UI_WIDGET) && !app.sleepAt && !app.petting &&
              !app.suppTouch && (now - app.lastShake) > 3000UL;
  if (!free) { app.tiltPhase = 0; return; }           // the menu, an alarm, a shake or a hand on its head come first

  uint8_t mood = (uint8_t)curMood();

  if (rock) {                                         // rocked back and forth
    app.dndIdle = now;
    app.tiltPhase = 0;
    app.tiltQuietUntil = now + 2200;
    if (rock >= 2) { react(EX_DIZZY, DIZZY_MS, true); app.rig.wobble = 1.0f; }
    else           react((Expression)TILT_ROCK[mood], 2000, true);
    rigImpulse(app.rig, -1.4f * M().bounce);
    return;
  }
  if (now < app.tiltQuietUntil) return;

  uint8_t ph = held < TILT_T_CURIOUS_MS ? 0 : held < TILT_T_UNEASY_MS ? 1 :
               held < TILT_T_UPSET_MS   ? 2 : held < TILT_T_CRY_MS    ? 3 : 4;
  if (ph != app.tiltPhase) {
    app.tiltPhase = ph;
    if (ph) {                                         // reached a new stage of being held
      app.dndIdle = now;
      react((Expression)TILT_FACES[mood][ph - 1], 600, ph >= 3);
      rigImpulse(app.rig, -0.9f * M().bounce);
    } else if (back >= TILT_RELIEF_MS) {              // set down after a long hold
      react((Expression)TILT_RELIEF[mood], 1800, true);
      rigImpulse(app.rig, -1.2f * M().bounce);
    }
  } else if (ph) {                                    // keep the face while it stays tipped
    app.reactFace = (Expression)TILT_FACES[mood][ph - 1];
    app.reactUntil = now + 500;
  }
}

static void openMenu() {
  app.returnMode = (app.mode == UI_WIDGET) ? UI_WIDGET : UI_FACE;
  app.mode = UI_MENU; app.menuItem = 0; app.menuSub = 0; app.subView = 0;
  sfx(SFX_CLICK);
}

static void onGesture(TouchGesture g, uint32_t now) {
  switch (g) {
    case TG_PET_START:
      app.dndIdle = now;                                 // interaction: DND idle clock restarts
      beginPetting(now - app.touch.petElapsed(now));
      break;
    case TG_PET_END:
      endPetting(false);
      break;
    case TG_TAP:
      if (app.mode == UI_WIDGET) {                    // tap = next widget
        app.widget = widgetNext(app.widget, settings.widgets);
        rigImpulse(app.rig, -1.0f);
        sfx(SFX_CLICK);
      } else {
        app.dndIdle = now;                            // boop = interaction
        appPet();
      }
      break;
    case TG_DOUBLE:                                   // double tap = widgets on / off
      if (app.mode == UI_FACE) { app.mode = UI_WIDGET; app.widget = widgetNext(WG_COUNT - 1, settings.widgets); }
      else app.mode = UI_FACE;
      app.returnMode = app.mode;
      rigImpulse(app.rig, -1.6f);
      sfx(SFX_OK);
      break;
    case TG_TRIPLE:                                   // triple tap = menu
      openMenu();
      break;
    default: break;
  }
}

void appBegin() {
  app.mode = UI_FACE;
  app.returnMode = UI_FACE;
  app.bootMs = millis();
  app.lastActive = millis();
  app.dndIdle = millis();
  app.dndSlept = false;
  app.sideSign = (rand32() & 1) ? 1 : -1;
  app.suppTouch = false;
  app.subView = 0; app.menuSub = 0;
  rigInit(app.rig);
  rigCloseEyes(app.rig);
  app.appliedFace = app.appliedPack = app.appliedMood = -1;
  app.face = appHomeFace();
  app.nextBlink = millis() + 1500;
  app.nextLook = millis() + 1200;
  app.nextAutoFace = millis() + M().chgMin;
  app.nextScene = millis() + (M().sceneMax ? rrange(M().sceneMin, M().sceneMax) : 0);
  app.lastPomo = pomoState();
  lastMinKey = -1;
}

void appTick(float dt) {
  timeService();                      // alarms fire even while the display sleeps
  uint32_t now = millis();

  // the do-not-disturb window ended (or DND was switched off): wake the buddy
  // we put to bed ourselves. A manually scheduled sleep stays.
  if (app.dndSlept && !dndActiveNow()) {
    app.dndSlept = false;
    if (app.mode == UI_SLEEP || app.sleepAt || app.face == EX_SLEEP) appWake();
  }

  // back on the home face (from widgets, the menu or a ringing alarm):
  // the do-not-disturb idle clock starts over
  static uint8_t lastMode = UI_FACE;
  if (app.mode != lastMode) {
    if (app.mode == UI_FACE && !app.sleepAt) app.dndIdle = now;
    lastMode = app.mode;
  }

  if (app.mode == UI_SLEEP) {
    sensorsTiltReturned(); sensorsTiltRock();           // (not needed while asleep)
    // picked up and held for a moment wakes it - but a bump in the night (DND) does not
    if (sensorsTiltHeldMs() >= TILT_WAKE_MS && !dndActiveNow()) appWake();
    if (sensorsTouched()) { appWake(); app.suppTouch = true; }
    while (sensorsTouch() != TE_NONE) {}
    { bool dd; uint32_t mm; while (sensorsEdge(dd, mm)) {} }
    app.touch.reset();
    sensorsShake();
    return;
  }

  pomoTick();
  ringTimeoutTick(now);

  if (app.suppTouch) {
    while (sensorsTouch() != TE_NONE) {}
    if (!sensorsTouched()) app.suppTouch = false;
  }

  bool shook = sensorsShake();

  // ----- panel dimming: idle = no touch, tilt, shake, menu or alarm for a few minutes -----
  if (shook || sensorsTouched() || sensorsTiltHeldMs() || app.ringing || app.mode == UI_MENU || app.petting)
    app.lastActive = now;
  {
    bool dim = (now - app.lastActive) > OLED_DIM_AFTER_MS && (app.mode == UI_FACE || app.mode == UI_WIDGET);
    uint32_t t = dim ? ((uint32_t)settings.brightness * OLED_DIM_PERCENT) / 100UL : settings.brightness;
    if (t < 40) t = 40;
    if (t > settings.brightness) t = settings.brightness;      // never brighter than the user's setting
    if (app.appliedBr != (uint8_t)t) { app.appliedBr = (uint8_t)t; displaySetBrightness((uint8_t)t); }
  }
  if (app.ringing && app.mode != UI_RINGING && app.mode != UI_NOTIFY)
    app.mode = (app.ringType == AT_REMINDER) ? UI_NOTIFY : UI_RINGING;
  if (shook && (app.mode == UI_FACE || app.mode == UI_WIDGET) && !app.sleepAt && !app.petting) appShakeReact();

  // ----- face / widget screens: the petting engine reads the raw touches -----
  bool petMode = (app.mode == UI_FACE || app.mode == UI_WIDGET) && !app.sleepAt && !app.suppTouch;
  {
    bool d; uint32_t ems;
    while (sensorsEdge(d, ems)) {
      app.lastActive = now;
      if (app.sleepAt && d) { appWake(); app.touch.reset(); continue; }     // any touch cancels "going to sleep"
      if (petMode) {
        app.touch.edge(d, ems);
        if (d) rigImpulse(app.rig, -0.7f);                                  // instant little "I felt that" squish
      }
    }
  }
  if (petMode) {
    TouchGesture tg;
    while ((tg = app.touch.poll(now)) != TG_NONE) onGesture(tg, now);
  } else {
    app.touch.reset();
    endPetting(true);
  }

  // ----- menu / ringing: classic tap / hold events -----
  TouchEvt te;
  while ((te = sensorsTouch()) != TE_NONE) {
    if (app.sleepAt) continue;
    if (app.mode == UI_MENU) menuTick(te);
    else if (app.mode == UI_RINGING || app.mode == UI_NOTIFY) ringingTick(te);
    // face / widget screens ignore these: the petting engine decides there
  }

  // ----- do not disturb: only on the home face, never in a study session -----
  // 15 min untouched -> sleeping face; the panel goes dark at 30 min total.
  if (app.mode == UI_FACE && !app.sleepAt && !app.petting && pomoState() == PO_IDLE &&
      dndActiveNow() && (now - app.dndIdle) >= DND_IDLE_SLEEP_MS) {
    app.dndSlept = true;
    appSleepNow(DND_IDLE_DEEP_MS);       // eyes shut now, panel off after another stretch
    Serial.println("[DND] idle on the home face - drifting to sleep");
  }

  if (app.sleepAt && now >= app.sleepAt) {         // eyes are shut: power the panel down
    app.sleepAt = 0;
    app.mode = UI_SLEEP;
    displaySleep(true);
    return;
  }

  tiltTick(now);
  if (app.mode == UI_FACE || app.mode == UI_WIDGET) faceModeTick();
  if (app.petting) pettingTick(now);

  // study glasses go on for the whole focus session and come off on break
  app.rig.glassesT = (pomoState() == PO_FOCUS) ? 1.0f : 0.0f;

  // ----- point the rig at what we should look like right now -----
  Expression eff = effectiveFace();
  bool faceChanged = (app.appliedFace != (int)eff);
  bool first = (app.appliedFace < 0);
  if (faceChanged || app.appliedPack != (int)settings.pack || app.appliedMood != (int)settings.mood) {
    rigSetFace(app.rig, eff, curPack(), curMood());
    app.curFlags = faceFlags(eff);
    if (faceChanged) app.sideSign = (rand32() & 1) ? 1 : -1;
    if (faceChanged && !first) rigImpulse(app.rig, -1.3f * M().bounce);
    app.appliedFace = eff; app.appliedPack = settings.pack; app.appliedMood = settings.mood;
  }
  // every emotion has a voice (previews of eye style / mood stay quiet)
  bool previewing = (app.mode == UI_MENU && (app.menuSub == 2 || app.menuSub == 3));
  if (((faceChanged && !first) || app.emoKick) && !previewing) emoSound(eff, app.emoKick);
  app.emoKick = false;

  if (app.mode == UI_FACE || app.mode == UI_WIDGET || app.mode == UI_RINGING || app.mode == UI_NOTIFY ||
      (app.mode == UI_MENU && app.menuSub)) {
    gazeTick(app.curFlags);
    if (app.mode != UI_RINGING) blinkTick();
  }
  if (app.mode == UI_RINGING) app.rig.wobble = 1.0f;

  rigTick(app.rig, dt);
  rigBlink(app.rig, dt);

  // ----- render -----
  U8G2& g = display();
  g.clearBuffer();
  g.setDrawColor(1);
  switch (app.mode) {
    case UI_FACE:
      drawFaceScreen(g);
      break;
    case UI_WIDGET:
      drawWidgetMode(g);
      break;
    case UI_MENU:
      if (app.menuSub) drawBrowser(g);
      else if (app.subView == 1) drawInfoScreen(g);
      else if (app.subView == 2) drawSensorScreen(g);
      else if (app.subView == 4) drawAgenda(g);
      else if (app.subView == 5) drawTasksDay(g);
      else drawMenu(g);
      break;
    case UI_RINGING:
      drawRinging(g);
      break;
    case UI_NOTIFY:
      drawNotify(g);
      break;
    default: break;
  }
  g.sendBuffer();
}
