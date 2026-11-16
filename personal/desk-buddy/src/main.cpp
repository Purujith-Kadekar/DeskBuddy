// ============================================================
//  DESK BUDDY  -  ESP32-C3 + SH1106 OLED + TTP223 + tilt + buzzer
//  Square eyes, long-press menu, Pomodoro, web settings page
// ============================================================
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <esp_system.h>
#include <math.h>
#include <stdio.h>

// ===== WIFI: FILL THESE IN =====================================
#define WIFI_SSID  ""
#define WIFI_PASS  ""
#define MDNS_NAME  "deskbuddy"
// ===============================================================

// ===== PINS ====================================================
#define SDA_PIN    4
#define SCL_PIN    5
#define TILT_PIN   3
#define TOUCH_PIN  1
#define BUZZER_PIN 2
// ===============================================================

U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
WebServer server(80);
Preferences prefs;

// ===== LAYOUT & TIMING =========================================
const float LEFT_CX = 38, RIGHT_CX = 90, EYE_CY = 28;
const float MOUTH_X = 64, MOUTH_Y = 46;

const uint32_t FRAME_MS     = 30;
const uint32_t TAP_MS       = 450;
const uint32_t LOVE_MS      = 600;
const uint32_t MENU_OPEN_MS = 2000;
const uint32_t MENU_PICK_MS = 700;
const uint32_t MENU_IDLE_MS = 15000;
const uint32_t IP_SHOW_MS   = 6000;
const uint32_t SLEEPY_AFTER = 25000;
const uint32_t SLEEP_AFTER  = 60000;

// ===== ENUMS ===================================================
enum Mood : uint8_t {
  M_CUTE, M_HAPPY, M_LAUGH, M_LOVE, M_WINK, M_SHY, M_SURPRISED,
  M_ANGRY, M_SAD, M_CRY, M_SLEEPY, M_SLEEP, M_FOCUS, M_CHILL,
  M_DIZZY, M_BORED, M_CONFUSED
};
enum Shape : uint8_t { S_BOX, S_UP, S_DOWN, S_HEART, S_X, S_LINE };
enum MouthK : uint8_t { MK_NONE, MK_CURVE, MK_FLAT, MK_W, MK_WAVY, MK_O, MK_D };
enum Phase : uint8_t { PH_IDLE, PH_FOCUS, PH_BREAK };
enum Mode : uint8_t { MODE_EYES, MODE_MENU, MODE_IP };
enum MenuItem : uint8_t { MI_TIMER, MI_STOP, MI_SOUND, MI_IP, MI_BACK, MI_COUNT };
enum PKind : uint8_t { P_HEART, P_SPARK, P_TEAR, P_Z, P_BANG, P_QMARK };

struct Pose {
  float w, h, lidL, lidR, tilt, gx, gy, mw, mc, mo;
  uint8_t sL, sR, mk;
};

// ===== SETTINGS (flash) ========================================
struct Settings {
  uint8_t studyMin = 25;
  uint8_t shortMin = 5;
  uint8_t longMin  = 15;
  uint8_t cycles   = 4;
  bool soundOn     = true;
};
Settings cfg;

void loadSettings() {
  prefs.begin("buddy", true);
  cfg.studyMin = prefs.getUChar("study", 25);
  cfg.shortMin = prefs.getUChar("short", 5);
  cfg.longMin  = prefs.getUChar("long", 15);
  cfg.cycles   = prefs.getUChar("cycles", 4);
  cfg.soundOn  = prefs.getBool("sound", true);
  prefs.end();
}

void saveSettings() {
  prefs.begin("buddy", false);
  prefs.putUChar("study", cfg.studyMin);
  prefs.putUChar("short", cfg.shortMin);
  prefs.putUChar("long", cfg.longMin);
  prefs.putUChar("cycles", cfg.cycles);
  prefs.putBool("sound", cfg.soundOn);
  prefs.end();
}

// ===== SOUND ===================================================
struct Note { uint16_t f; uint16_t d; };   // f == 0 is a rest

const Note SND_HAPPY[] = {{2000,70},{0,20},{2600,70},{0,20},{3200,120}};
const Note SND_SHOCK[] = {{900,70},{0,30},{600,150}};
const Note SND_ANNOY[] = {{320,110},{0,40},{260,110},{0,40},{200,220}};
const Note SND_DIZZY[] = {{700,60},{900,60},{600,60},{1000,60},{500,120}};
const Note SND_CRY[]   = {{600,220},{0,30},{500,220},{0,30},{380,380}};
const Note SND_CHIRP[] = {{1800,30},{0,20},{2400,40}};
const Note SND_LOVE[]  = {{1400,90},{1800,90},{2200,160}};
const Note SND_MENU[]  = {{1200,40}};
const Note SND_START[] = {{1500,80},{0,40},{2000,80},{0,40},{2600,120}};
const Note SND_BREAK[] = {{1100,120},{0,60},{1400,120},{0,60},{1700,200}};
const Note SND_DONE[]  = {{2400,200},{0,150},{2400,200},{0,150},{2400,400}};
const Note SND_STOP[]  = {{900,120},{0,40},{600,200}};
const Note SND_SLEEP[] = {{500,150},{0,30},{300,260}};
const Note SND_WAKE[]  = {{500,70},{800,70},{1100,120}};

const Note* seqPtr = nullptr;
uint8_t seqLen = 0, seqIdx = 0;
uint32_t seqNext = 0;

template <size_t N>
void play(const Note (&s)[N]) {
  if (!cfg.soundOn) return;
  seqPtr = s; seqLen = N; seqIdx = 0; seqNext = 0;
}

void updateSound(uint32_t now) {
  if (!seqPtr || now < seqNext) return;
  if (seqIdx >= seqLen) {
    noTone(BUZZER_PIN);
    seqPtr = nullptr;
    return;
  }
  Note n = seqPtr[seqIdx++];
  if (n.f) tone(BUZZER_PIN, n.f);
  else     noTone(BUZZER_PIN);
  seqNext = now + n.d;
}

// ===== STATE ===================================================
Mode mode = MODE_EYES;
Mood mood = M_CUTE;
Mood timedMood = M_CUTE;
uint32_t timedUntil = 0;
uint32_t lastInteract = 0;
uint32_t nextBlinkAt = 0, blinkStart = 0;
bool blinking = false;
uint32_t nextGlance = 0, nextWander = 0, lastFrame = 0;
float glanceX = 0, glanceY = 0;
Pose cur, tgt;

// touch
bool pressed = false, holdDone = false;
uint32_t pressAt = 0;

// menu
uint8_t menuCur = 0;
uint32_t menuTouchAt = 0;
uint32_t ipShownAt = 0;

// pomodoro
Phase phase = PH_IDLE;
bool paused = false;
bool breakIsLong = false;
uint32_t phaseTotal = 0;
uint32_t phaseEnd = 0;
uint32_t pausedLeft = 0;
uint16_t completedFocus = 0;

// network
bool serverUp = false;
bool mdnsUp = false;
String ipStr = "";

// shake
volatile uint32_t shakeCount = 0;
volatile uint32_t lastShakeIsr = 0;

// particles
struct Part { bool on; uint8_t kind; float x, y, vy; uint8_t life; };
Part parts[8];

void setTimed(Mood m, uint32_t dur, uint32_t now) {
  timedMood = m;
  timedUntil = now + dur;
}

// ===== POMODORO ================================================
uint32_t remainingMs(uint32_t now) {
  if (phase == PH_IDLE) return 0;
  if (paused) return pausedLeft;
  int32_t r = (int32_t)(phaseEnd - now);
  return r > 0 ? (uint32_t)r : 0;
}

void enterBreak(uint32_t now) {
  breakIsLong = completedFocus > 0 && (completedFocus % cfg.cycles) == 0;
  phase = PH_BREAK;
  paused = false;
  phaseTotal = (uint32_t)(breakIsLong ? cfg.longMin : cfg.shortMin) * 60000UL;
  phaseEnd = now + phaseTotal;
  play(SND_BREAK);
  setTimed(M_HAPPY, 1500, now);
}

void startFocus(uint32_t now) {
  phase = PH_FOCUS;
  paused = false;
  breakIsLong = false;
  phaseTotal = (uint32_t)cfg.studyMin * 60000UL;
  phaseEnd = now + phaseTotal;
  play(SND_START);
  setTimed(M_HAPPY, 1200, now);
}

void stopTimer(uint32_t now) {
  phase = PH_IDLE;
  paused = false;
  play(SND_STOP);
  setTimed(M_SAD, 1500, now);
}

void togglePause(uint32_t now) {
  if (phase == PH_IDLE) return;
  if (!paused) {
    pausedLeft = remainingMs(now);
    paused = true;
  } else {
    phaseEnd = now + pausedLeft;
    paused = false;
  }
  play(SND_CHIRP);
}

void skipPhase(uint32_t now) {
  if (phase == PH_FOCUS) {
    completedFocus++;
    enterBreak(now);
  } else if (phase == PH_BREAK) {
    phase = PH_IDLE;
    paused = false;
  }
}

void updateTimer(uint32_t now) {
  if (phase == PH_IDLE || paused) return;
  if ((int32_t)(now - phaseEnd) < 0) return;
  if (phase == PH_FOCUS) {
    completedFocus++;
    enterBreak(now);
  } else {
    phase = PH_IDLE;
    play(SND_DONE);
    setTimed(M_WINK, 2500, now);
  }
}

// ===== MENU ====================================================
void openMenu(uint32_t now) {
  mode = MODE_MENU;
  menuCur = 0;
  menuTouchAt = now;
  play(SND_CHIRP);
}

void selectMenu(uint32_t now) {
  menuTouchAt = now;
  switch (menuCur) {
    case MI_TIMER:
      if (phase == PH_IDLE) startFocus(now);
      else togglePause(now);
      break;
    case MI_STOP:
      if (phase != PH_IDLE) stopTimer(now);
      break;
    case MI_SOUND:
      cfg.soundOn = !cfg.soundOn;
      saveSettings();
      if (!cfg.soundOn) { seqPtr = nullptr; noTone(BUZZER_PIN); }
      else play(SND_CHIRP);
      break;
    case MI_IP:
      mode = MODE_IP;
      ipShownAt = now;
      break;
    case MI_BACK:
      mode = MODE_EYES;
      lastInteract = now;
      setTimed(M_HAPPY, 800, now);
      play(SND_CHIRP);
      break;
  }
}

// ===== TOUCH ===================================================
void onTouchDown(uint32_t now) {
  if (mood == M_SLEEP || mood == M_SLEEPY) play(SND_WAKE);
  lastInteract = now;
  pressAt = now;
  holdDone = false;
}

void onTouchRelease(uint32_t dur, uint32_t now) {
  lastInteract = now;
  if (holdDone) return;
  if (dur >= TAP_MS) return;

  if (mode == MODE_EYES) {
    setTimed(M_HAPPY, 1000, now);
    play(SND_HAPPY);
  } else if (mode == MODE_MENU) {
    menuCur = (menuCur + 1) % MI_COUNT;
    menuTouchAt = now;
    play(SND_MENU);
  } else if (mode == MODE_IP) {
    mode = MODE_EYES;
    lastInteract = now;
  }
}

void pollTouch(uint32_t now) {
  static bool raw = false;
  static uint32_t changeAt = 0;

  bool r = digitalRead(TOUCH_PIN) == HIGH;
  if (r != raw) { raw = r; changeAt = now; }

  if (raw != pressed && now - changeAt >= 25) {
    pressed = raw;
    if (pressed) onTouchDown(now);
    else         onTouchRelease(now - pressAt, now);
  }

  if (pressed && !holdDone) {
    uint32_t d = now - pressAt;
    if (mode == MODE_EYES && d >= MENU_OPEN_MS) {
      holdDone = true;
      openMenu(now);
    } else if (mode == MODE_MENU && d >= MENU_PICK_MS) {
      holdDone = true;
      selectMenu(now);
    }
  }
}

// ===== SHAKE ===================================================
void IRAM_ATTR onTilt() {
  uint32_t n = millis();
  if (n - lastShakeIsr > 150) {
    shakeCount = shakeCount + 1;
    lastShakeIsr = n;
  }
}

void handleShake(uint32_t now) {
  static uint32_t seen = 0;
  static uint32_t hits[6] = {0, 0, 0, 0, 0, 0};
  static uint8_t hi = 0;
  static uint32_t lastAngry = 0;

  uint32_t c = shakeCount;
  if (mode != MODE_EYES) { seen = c; return; }
  if (c == seen) return;
  uint32_t n = c - seen;
  seen = c;
  lastInteract = now;

  for (uint32_t k = 0; k < n && k < 6; k++) {
    hits[hi] = now;
    hi = (hi + 1) % 6;
  }

  uint8_t c2 = 0, c3 = 0;
  for (int i = 0; i < 6; i++) {
    if (!hits[i]) continue;
    uint32_t age = now - hits[i];
    if (age < 2000) c2++;
    if (age < 3000) c3++;
  }

  if (c3 >= 6) {
    setTimed(M_DIZZY, 2500, now);
    play(SND_DIZZY);
    for (int i = 0; i < 6; i++) hits[i] = 0;
  } else if (c2 >= 3) {
    if (lastAngry && now - lastAngry < 15000) {
      setTimed(M_CRY, 2500, now);
      play(SND_CRY);
    } else {
      setTimed(M_ANGRY, 2000, now);
      play(SND_ANNOY);
    }
    lastAngry = now;
  } else {
    setTimed(M_SURPRISED, 800, now);
    play(SND_SHOCK);
  }
}

// ===== MOOD SELECTION ==========================================
Mood wanderPick() {
  static const Mood pool[] = {
    M_CUTE, M_CUTE, M_CUTE, M_WINK, M_SHY, M_BORED,
    M_CONFUSED, M_HAPPY, M_SAD, M_LAUGH, M_SURPRISED
  };
  return pool[random(sizeof(pool) / sizeof(pool[0]))];
}

Mood pickMood(uint32_t now) {
  if (mode == MODE_EYES && pressed && !holdDone && now - pressAt >= LOVE_MS) return M_LOVE;
  if (now < timedUntil) return timedMood;
  if (phase == PH_FOCUS) return paused ? M_BORED : M_FOCUS;
  if (phase == PH_BREAK) return M_CHILL;

  uint32_t idle = now - lastInteract;
  if (idle > SLEEP_AFTER)  return M_SLEEP;
  if (idle > SLEEPY_AFTER) return M_SLEEPY;

  if (now >= nextWander) {
    nextWander = now + random(6000, 12000);
    setTimed(wanderPick(), random(2000, 4000), now);
    return timedMood;
  }
  return M_CUTE;
}

// ===== POSES ===================================================
Pose poseFor(Mood m, float t) {
  Pose p;
  p.w = 24; p.h = 28; p.lidL = p.lidR = 0; p.tilt = 0;
  p.gx = p.gy = 0; p.mw = 10; p.mc = 2; p.mo = 0;
  p.sL = p.sR = S_BOX; p.mk = MK_W;

  switch (m) {
    case M_CUTE:
      p.h = 28 + sinf(t * 1.6f) * 0.8f;
      p.gy = sinf(t * 2.2f) * 0.8f;
      break;
    case M_HAPPY:
      p.sL = p.sR = S_UP; p.w = 22;
      p.mk = MK_CURVE; p.mc = 5; p.mw = 14;
      p.gy = sinf(t * 7) * 0.9f;
      break;
    case M_LAUGH:
      p.sL = p.sR = S_UP; p.w = 22;
      p.mk = MK_D; p.mo = 4.5f; p.mw = 12;
      p.gy = sinf(t * 12) * 0.8f;
      break;
    case M_LOVE:
      p.sL = p.sR = S_HEART; p.w = 22; p.h = 22;
      p.mk = MK_CURVE; p.mc = 4; p.mw = 12;
      p.gy = sinf(t * 4) * 0.9f;
      break;
    case M_WINK:
      p.sL = S_UP;
      p.mk = MK_CURVE; p.mc = 5; p.mw = 12;
      break;
    case M_SHY:
      p.w = 18; p.h = 20; p.gy = 4; p.gx = sinf(t * 1.2f) * 2;
      p.mk = MK_W; p.mw = 6;
      break;
    case M_SURPRISED:
      p.w = 28; p.h = 32; p.mk = MK_O; p.mo = 4;
      break;
    case M_ANGRY:
      p.w = 26; p.h = 22; p.lidL = p.lidR = 0.3f; p.tilt = 6;
      p.mk = MK_CURVE; p.mc = -4; p.mw = 12;
      break;
    case M_SAD:
      p.w = 22; p.h = 26; p.lidL = p.lidR = 0.25f; p.tilt = -4; p.gy = 1;
      p.mk = MK_CURVE; p.mc = -4; p.mw = 10;
      break;
    case M_CRY:
      p.lidL = p.lidR = 0.1f;
      p.mk = MK_WAVY; p.mw = 14;
      p.gy = sinf(t * 10) * 0.6f;
      break;
    case M_SLEEPY:
      p.h = 26; p.lidL = p.lidR = 0.6f + sinf(t * 1.4f) * 0.03f;
      p.mk = MK_O; p.mo = 2; p.gy = 1;
      break;
    case M_SLEEP:
      p.sL = p.sR = S_DOWN; p.w = 22; p.h = 14;
      p.mk = MK_O; p.mo = 1.5f + sinf(t * 1.1f) * 0.6f; p.gy = 1.5f;
      break;
    case M_FOCUS:
      p.h = 20; p.lidL = p.lidR = 0.18f;
      p.mk = MK_FLAT; p.mw = 8;
      break;
    case M_CHILL:
      p.h = 24; p.lidL = p.lidR = 0.35f;
      p.mk = MK_CURVE; p.mc = 3; p.mw = 10;
      p.gy = sinf(t * 1.5f);
      break;
    case M_DIZZY:
      p.sL = p.sR = S_X; p.w = 14; p.h = 14;
      p.mk = MK_WAVY; p.mw = 10;
      p.gx = sinf(t * 12) * 2;
      break;
    case M_BORED:
      p.h = 26; p.lidL = p.lidR = 0.5f;
      p.mk = MK_FLAT; p.mw = 10;
      p.gx = sinf(t * 0.7f) * 4;
      break;
    case M_CONFUSED:
      p.tilt = -3; p.mk = MK_WAVY; p.mw = 8; p.gx = 2; p.gy = -2;
      break;
  }
  return p;
}

// ===== PRIMITIVES ==============================================
float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void seg(float a, float b, float c, float d) {
  u8g2.drawLine((int)roundf(a), (int)roundf(b), (int)roundf(c), (int)roundf(d));
}

void drawCentered(const char* s, int y) {
  u8g2.drawStr((128 - u8g2.getStrWidth(s)) / 2, y, s);
}

void curve(float x0, float y0, float cx, float cy, float x1, float y1, uint8_t thick) {
  for (uint8_t o = 0; o < thick; o++) {
    float px = x0, py = y0 + o;
    for (int i = 1; i <= 10; i++) {
      float t = i / 10.0f, mt = 1 - t;
      float x = mt * mt * x0 + 2 * mt * t * cx + t * t * x1;
      float y = mt * mt * y0 + 2 * mt * t * cy + t * t * y1 + o;
      seg(px, py, x, y);
      px = x; py = y;
    }
  }
}

void drawHeart(int x, int y, int r) {
  u8g2.drawDisc(x - r, y - r / 2, r);
  u8g2.drawDisc(x + r, y - r / 2, r);
  u8g2.drawTriangle(x - 2 * r, y - r / 2 + 1, x + 2 * r, y - r / 2 + 1, x, y + 2 * r);
}

void drawX(float cx, float cy, float s) {
  for (int o = 0; o < 2; o++) {
    seg(cx - s, cy - s + o, cx + s, cy + s + o);
    seg(cx - s, cy + s + o, cx + s, cy - s + o);
  }
}

// ===== EYES ====================================================
// Square rounded eye. The lid is a black wedge carved from the top,
// tilted for angry (+) and sad (-) expressions.
void drawBoxEye(float cx, float cy, float w, float h, float lidPx, float tiltPx, bool left) {
  float x = cx - w * 0.5f, y = cy - h * 0.5f;
  int r = (int)fminf(6.0f, h * 0.5f);
  u8g2.drawRBox((int)roundf(x), (int)roundf(y), (int)roundf(w), (int)roundf(h), r);

  if (lidPx < 0.5f) return;

  float lo = clampf(lidPx - tiltPx, 0, h);
  float li = clampf(lidPx + tiltPx, 0, h);
  float fyL = left ? lo : li;
  float fyR = left ? li : lo;

  int X0 = (int)roundf(x) - 2;
  int X1 = (int)roundf(x + w) + 2;
  int Y0 = (int)roundf(y) - 2;
  int yl = (int)roundf(y + fyL);
  int yr = (int)roundf(y + fyR);

  u8g2.setDrawColor(0);
  u8g2.drawTriangle(X0, Y0, X1, Y0, X1, yr);
  u8g2.drawTriangle(X0, Y0, X1, yr, X0, yl);
  u8g2.setDrawColor(1);
}

void drawArcEye(float cx, float cy, float w, float h, bool happy) {
  float ye = happy ? cy + h * 0.18f : cy - h * 0.18f;
  float yc = happy ? cy - h * 0.9f  : cy + h * 0.9f;
  curve(cx - w * 0.5f, ye, cx, yc, cx + w * 0.5f, ye, happy ? 3 : 2);
}

void drawEye(uint8_t s, float cx, float cy, float w, float h, float lidPx, float tiltPx, bool left) {
  switch (s) {
    case S_BOX:   drawBoxEye(cx, cy, w, h, lidPx, tiltPx, left); break;
    case S_UP:    drawArcEye(cx, cy, w, h, true);  break;
    case S_DOWN:  drawArcEye(cx, cy, w, h, false); break;
    case S_HEART: drawHeart((int)roundf(cx), (int)roundf(cy), (int)roundf(w * 0.2f)); break;
    case S_X:     drawX(cx, cy, w * 0.32f); break;
    case S_LINE:
      seg(cx - w * 0.35f, cy, cx + w * 0.35f, cy);
      seg(cx - w * 0.35f, cy + 1, cx + w * 0.35f, cy + 1);
      break;
  }
}

// ===== MOUTHS ==================================================
void drawMouth(uint8_t k, float mx, float my, float mw, float mc, float mo) {
  switch (k) {
    case MK_CURVE:
      curve(mx - mw / 2, my, mx, my + mc * 2, mx + mw / 2, my, 2);
      break;
    case MK_FLAT:
      seg(mx - mw / 2, my, mx + mw / 2, my);
      seg(mx - mw / 2, my + 1, mx + mw / 2, my + 1);
      break;
    case MK_W:
      curve(mx - mw / 2, my - 1, mx - mw / 4, my + 3, mx, my - 1, 2);
      curve(mx, my - 1, mx + mw / 4, my + 3, mx + mw / 2, my - 1, 2);
      break;
    case MK_WAVY:
      curve(mx - mw / 2, my, mx - mw / 4, my - 3, mx, my, 2);
      curve(mx, my, mx + mw / 4, my + 3, mx + mw / 2, my, 2);
      break;
    case MK_O: {
      int r = max(1, (int)roundf(mo));
      u8g2.drawCircle((int)roundf(mx), (int)roundf(my), r);
      break;
    }
    case MK_D: {
      int r = max(2, (int)roundf(mo));
      int ix = (int)roundf(mx), iy = (int)roundf(my);
      u8g2.drawHLine(ix - r, iy, 2 * r + 1);
      for (int dy = 1; dy <= r; dy++) {
        int hw = (int)sqrtf((float)(r * r - dy * dy));
        u8g2.drawHLine(ix - hw, iy + dy, 2 * hw + 1);
      }
      break;
    }
    default: break;
  }
}

// ===== GLASSES (focus) =========================================
void drawGlasses() {
  u8g2.drawRFrame((int)LEFT_CX - 16,  (int)EYE_CY - 17, 32, 34, 7);
  u8g2.drawRFrame((int)LEFT_CX - 15,  (int)EYE_CY - 16, 30, 32, 6);
  u8g2.drawRFrame((int)RIGHT_CX - 16, (int)EYE_CY - 17, 32, 34, 7);
  u8g2.drawRFrame((int)RIGHT_CX - 15, (int)EYE_CY - 16, 30, 32, 6);
  u8g2.drawLine((int)LEFT_CX + 16, (int)EYE_CY - 3, (int)RIGHT_CX - 16, (int)EYE_CY - 3);
  u8g2.drawLine((int)LEFT_CX - 16, (int)EYE_CY - 8, 0, (int)EYE_CY - 12);
  u8g2.drawLine((int)RIGHT_CX + 16, (int)EYE_CY - 8, 127, (int)EYE_CY - 12);
}

// ===== PARTICLES ===============================================
void spawnPart(uint8_t kind, float x, float y, float vy, uint8_t life) {
  for (auto &p : parts) {
    if (!p.on) { p = {true, kind, x, y, vy, life}; return; }
  }
}

void updateParts() {
  u8g2.setFont(u8g2_font_ncenB08_tr);
  for (auto &p : parts) {
    if (!p.on) continue;
    p.y += p.vy;
    if (p.life == 0 || p.y < 0 || p.y > 63) { p.on = false; continue; }
    p.life--;
    int x = (int)roundf(p.x), y = (int)roundf(p.y);
    switch (p.kind) {
      case P_HEART:
        drawHeart(x, y, 3);
        break;
      case P_SPARK:
        u8g2.drawLine(x - 2, y, x + 2, y);
        u8g2.drawLine(x, y - 2, x, y + 2);
        break;
      case P_TEAR:
        u8g2.drawTriangle(x - 2, y - 1, x + 2, y - 1, x, y - 4);
        u8g2.drawDisc(x, y, 2);
        break;
      case P_Z:     u8g2.drawStr(x, y, "z"); break;
      case P_BANG:  u8g2.drawStr(x, y, "!"); break;
      case P_QMARK: u8g2.drawStr(x, y, "?"); break;
    }
  }
}

void onMoodEnter(Mood m) {
  switch (m) {
    case M_SURPRISED: spawnPart(P_BANG,  116, 10, 0, 22); break;
    case M_CONFUSED:  spawnPart(P_QMARK, 116, 10, 0, 30); break;
    case M_SAD:       spawnPart(P_TEAR, LEFT_CX, 42, 0.5f, 14); break;
    case M_HAPPY:
    case M_LAUGH:
      spawnPart(P_SPARK, 8, 8, 0, 10);
      spawnPart(P_SPARK, 118, 8, 0, 10);
      break;
    default: break;
  }
}

// ===== BLINK / GLANCE / TWEEN ==================================
float blinkAmount(uint32_t now) {
  if (!blinking) {
    if (now < nextBlinkAt) return 0;
    blinking = true;
    blinkStart = now;
  }
  float p = (now - blinkStart) / 130.0f;
  if (p >= 1.0f) {
    blinking = false;
    nextBlinkAt = now + random(2500, 5500);
    return 0;
  }
  return p < 0.5f ? p * 2 : (1 - p) * 2;
}

void updateGlance(uint32_t now) {
  if (now < nextGlance) return;
  glanceX = random(-3, 4);
  glanceY = random(-2, 2);
  nextGlance = now + random(2500, 5000);
}

void tween(float k) {
  cur.w  += (tgt.w  - cur.w)  * k;
  cur.h  += (tgt.h  - cur.h)  * k;
  cur.lidL += (tgt.lidL - cur.lidL) * k;
  cur.lidR += (tgt.lidR - cur.lidR) * k;
  cur.tilt += (tgt.tilt - cur.tilt) * k;
  cur.gx += (tgt.gx - cur.gx) * k;
  cur.gy += (tgt.gy - cur.gy) * k;
  cur.mw += (tgt.mw - cur.mw) * k;
  cur.mc += (tgt.mc - cur.mc) * k;
  cur.mo += (tgt.mo - cur.mo) * k;
  cur.sL = tgt.sL;
  cur.sR = tgt.sR;
  cur.mk = tgt.mk;
}

// ===== HUD =====================================================
void drawHud(uint32_t now) {
  if (phase == PH_IDLE || phaseTotal == 0) return;
  uint32_t rem = remainingMs(now);
  int bar = (int)(127.0f * rem / phaseTotal);
  u8g2.drawBox(0, 0, bar, 1);

  const char* label = phase == PH_FOCUS ? "FOCUS" : (breakIsLong ? "LONG BREAK" : "BREAK");
  if (paused) label = "PAUSED";
  uint32_t s = rem / 1000;
  char buf[24];
  snprintf(buf, sizeof(buf), "%s %02lu:%02lu", label,
           (unsigned long)(s / 60), (unsigned long)(s % 60));
  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(buf, 62);
}

// ===== RENDER: FACE ============================================
void renderFace(uint32_t now, float blink) {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);

  float bh = fmaxf(2.0f, cur.h * (1.0f - 0.92f * blink));
  float ex = cur.gx;
  float ey = EYE_CY + cur.gy;
  float lidL = cur.lidL * bh;
  float lidR = cur.lidR * bh;
  float tiltPx = cur.tilt * (bh / cur.h);

  drawEye(cur.sL, LEFT_CX + ex,  ey, cur.w, bh, lidL, tiltPx, true);
  drawEye(cur.sR, RIGHT_CX + ex, ey, cur.w, bh, lidR, tiltPx, false);

  drawMouth(cur.mk, MOUTH_X + ex * 0.3f, MOUTH_Y + cur.gy * 0.3f,
            cur.mw, cur.mc, cur.mo);

  if (phase == PH_FOCUS) drawGlasses();

  updateParts();
  drawHud(now);
  u8g2.sendBuffer();
}

// ===== RENDER: MENU ============================================
void iconPlay(int cx, int cy) {
  u8g2.drawTriangle(cx - 6, cy - 10, cx - 6, cy + 10, cx + 10, cy);
}

void iconPause(int cx, int cy) {
  u8g2.drawBox(cx - 9, cy - 10, 6, 20);
  u8g2.drawBox(cx + 3, cy - 10, 6, 20);
}

void iconStop(int cx, int cy) {
  u8g2.drawBox(cx - 9, cy - 9, 18, 18);
}

// Draws a sound-wave arc to the right of the speaker cone
void soundArc(int cx, int cy, int r) {
  int px = 0, py = 0;
  bool first = true;
  for (int a = -50; a <= 50; a += 5) {
    float rad = a * 0.0174533f;
    int x = cx + (int)roundf(r * cosf(rad));
    int y = cy + (int)roundf(r * sinf(rad));
    if (!first) u8g2.drawLine(px, py, x, y);
    px = x;
    py = y;
    first = false;
  }
}

void iconSpeaker(int cx, int cy) {
  u8g2.drawBox(cx - 14, cy - 4, 6, 9);
  u8g2.drawTriangle(cx - 8, cy - 4, cx + 2, cy - 13, cx + 2, cy + 13);
  u8g2.drawTriangle(cx - 8, cy - 4, cx + 2, cy + 13, cx - 8, cy + 4);
  if (cfg.soundOn) {
    soundArc(cx + 2, cy, 7);
    soundArc(cx + 2, cy, 12);
  } else {
    seg(cx + 7, cy - 6, cx + 15, cy + 6);
    seg(cx + 7, cy + 6, cx + 15, cy - 6);
  }
}
void iconBack(int cx, int cy) {
  u8g2.drawTriangle(cx - 14, cy, cx - 3, cy - 10, cx - 3, cy + 10);
  u8g2.drawBox(cx - 4, cy - 2, 16, 5);
}

void renderMenu() {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);

  for (int i = 0; i < MI_COUNT; i++) {
    int x = 64 - (MI_COUNT - 1) * 6 + i * 12;
    if (i == menuCur) u8g2.drawDisc(x, 4, 3);
    else              u8g2.drawCircle(x, 4, 2);
  }

  int cx = 64, cy = 26;
  const char* label = "";
  switch (menuCur) {
    case MI_TIMER:
      if (phase == PH_IDLE)      { iconPlay(cx, cy);  label = "START FOCUS"; }
      else if (paused)           { iconPlay(cx, cy);  label = "RESUME"; }
      else                       { iconPause(cx, cy); label = "PAUSE"; }
      break;
    case MI_STOP:
      iconStop(cx, cy);
      label = "STOP TIMER";
      break;
    case MI_SOUND:
      iconSpeaker(cx, cy);
      label = cfg.soundOn ? "SOUND ON" : "SOUND OFF";
      break;
    case MI_IP:
      u8g2.setFont(u8g2_font_ncenB14_tr);
      drawCentered("IP", cy + 6);
      label = "SHOW ADDRESS";
      break;
    case MI_BACK:
      iconBack(cx, cy);
      label = "BACK";
      break;
  }

  u8g2.setFont(u8g2_font_6x10_tf);
  drawCentered(label, 52);
  u8g2.setFont(u8g2_font_5x7_tf);
  drawCentered("tap next / hold pick", 63);
  u8g2.sendBuffer();
}

String wifiStatusText();
void renderIp() {
  u8g2.clearBuffer();
  u8g2.setDrawColor(1);
  u8g2.setFont(u8g2_font_6x10_tf);
  if (serverUp && WiFi.status() == WL_CONNECTED) {
    drawCentered("open in browser:", 14);
    drawCentered(ipStr.c_str(), 32);
    String host = String("http://") + MDNS_NAME + ".local";
    drawCentered(host.c_str(), 48);
  } else {
    String s = wifiStatusText();
    drawCentered("no wifi", 20);
    drawCentered(s.c_str(), 36);
    drawCentered("check name and password", 52);
  }
  u8g2.sendBuffer();
}

// ===== WEB SERVER ==============================================
struct MoodName { const char* name; Mood mood; };
const MoodName MOOD_NAMES[] = {
  {"cute", M_CUTE}, {"happy", M_HAPPY}, {"laugh", M_LAUGH}, {"love", M_LOVE},
  {"wink", M_WINK}, {"shy", M_SHY}, {"surprised", M_SURPRISED}, {"angry", M_ANGRY},
  {"sad", M_SAD}, {"cry", M_CRY}, {"sleepy", M_SLEEPY}, {"sleep", M_SLEEP},
  {"focus", M_FOCUS}, {"chill", M_CHILL}, {"dizzy", M_DIZZY}, {"bored", M_BORED},
  {"confused", M_CONFUSED}
};

const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Desk Buddy</title>
<style>
body{font-family:system-ui,sans-serif;background:#ffe8f1;margin:0;padding:16px;color:#5a2a40}
.card{background:#fff;border-radius:20px;padding:16px;margin:0 auto 14px;max-width:420px;box-shadow:0 4px 14px #f5b5cc66}
h1{text-align:center;margin:8px 0;font-size:24px}
h3{margin:4px 0 10px}
.big{font-size:46px;text-align:center;font-variant-numeric:tabular-nums;margin:6px 0}
.label{text-align:center;font-weight:700;letter-spacing:2px;color:#d6457a}
.sub{text-align:center;font-size:14px;color:#9a6a80}
.row{text-align:center}
button{border:0;border-radius:14px;padding:10px 14px;margin:4px;background:#ff8fb1;color:#fff;font-size:15px;font-weight:600;cursor:pointer}
button.alt{background:#ffd1e0;color:#7a3050}
.grid{display:grid;grid-template-columns:repeat(3,1fr);gap:6px}
.grid button{margin:0;width:100%}
label{display:flex;justify-content:space-between;align-items:center;margin:10px 0;font-size:15px}
input[type=number]{width:70px;padding:6px;border-radius:10px;border:1px solid #f3b6cb}
</style></head><body>
<h1>Desk Buddy &#9825;</h1>
<div class="card">
  <div class="label" id="phase">READY</div>
  <div class="big" id="time">25:00</div>
  <div class="sub" id="done">0 focus sessions done</div>
</div>
<div class="card row">
  <button onclick="cmd('start')">&#9654; Focus</button>
  <button class="alt" onclick="cmd('pause')">&#10074;&#10074; Pause</button>
  <button class="alt" onclick="cmd('skip')">&#9197; Skip</button>
  <button class="alt" onclick="cmd('stop')">&#9632; Stop</button>
</div>
<div class="card">
  <h3>Pomodoro settings</h3>
  <label>Focus (minutes) <input type="number" id="study" min="1" max="120"></label>
  <label>Short break (minutes) <input type="number" id="short" min="1" max="60"></label>
  <label>Long break (minutes) <input type="number" id="long" min="1" max="60"></label>
  <label>Long break every (focus sessions) <input type="number" id="cycles" min="1" max="10"></label>
  <label>Sound <input type="checkbox" id="sound"></label>
  <div class="row"><button onclick="save()">Save</button></div>
</div>
<div class="card">
  <h3>Mood preview</h3>
  <div class="grid" id="moods"></div>
</div>
<script>
const MOODS=['cute','happy','laugh','love','wink','shy','surprised','angry','sad','cry','sleepy','sleep','focus','chill','dizzy','bored','confused'];
const $=id=>document.getElementById(id);
const mmss=s=>String(Math.floor(s/60)).padStart(2,'0')+':'+String(s%60).padStart(2,'0');
let loaded=false;
function show(s){
  const names={idle:'READY',focus:'FOCUS',break:'BREAK'};
  $('phase').textContent=(names[s.phase]||'READY')+(s.paused?' · PAUSED':'');
  $('time').textContent=s.phase==='idle'?mmss(s.study*60):mmss(s.remaining);
  $('done').textContent=s.done+' focus sessions done';
  if(!loaded){
    $('study').value=s.study; $('short').value=s.short;
    $('long').value=s.long; $('cycles').value=s.cycles;
    $('sound').checked=s.sound; loaded=true;
  }
}
async function get(u){ const r=await fetch(u); return r.json(); }
async function refresh(){ try{ show(await get('/api/status')); }catch(e){} }
function cmd(c){ get('/api/cmd?do='+c).then(show); }
function save(){
  const q=new URLSearchParams({study:$('study').value,short:$('short').value,
    long:$('long').value,cycles:$('cycles').value,sound:$('sound').checked?'1':'0'});
  get('/api/set?'+q).then(s=>{show(s);alert('saved');});
}
function mood(m){ get('/api/mood?m='+m); }
MOODS.forEach(m=>{
  const b=document.createElement('button');
  b.textContent=m; b.className='alt'; b.onclick=()=>mood(m);
  $('moods').appendChild(b);
});
refresh(); setInterval(refresh,1000);
</script></body></html>
)HTML";

void sendStatus() {
  uint32_t rem = remainingMs(millis()) / 1000;
  const char* ph = phase == PH_FOCUS ? "focus" : (phase == PH_BREAK ? "break" : "idle");
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"phase\":\"%s\",\"paused\":%s,\"remaining\":%lu,\"done\":%u,"
    "\"study\":%u,\"short\":%u,\"long\":%u,\"cycles\":%u,\"sound\":%s}",
    ph, paused ? "true" : "false", (unsigned long)rem, (unsigned)completedFocus,
    cfg.studyMin, cfg.shortMin, cfg.longMin, cfg.cycles,
    cfg.soundOn ? "true" : "false");
  server.send(200, "application/json", buf);
}

int argInt(const char* name, int lo, int hi, int fallback) {
  if (!server.hasArg(name)) return fallback;
  return constrain(server.arg(name).toInt(), lo, hi);
}

void handleRoot()   { server.send(200, "text/html; charset=utf-8", INDEX_HTML); }
void handleStatus() { sendStatus(); }

void handleSet() {
  cfg.studyMin = argInt("study",  1, 120, cfg.studyMin);
  cfg.shortMin = argInt("short",  1, 60,  cfg.shortMin);
  cfg.longMin  = argInt("long",   1, 60,  cfg.longMin);
  cfg.cycles   = argInt("cycles", 1, 10,  cfg.cycles);
  if (server.hasArg("sound")) {
    cfg.soundOn = server.arg("sound") == "1";
    if (!cfg.soundOn) { seqPtr = nullptr; noTone(BUZZER_PIN); }
  }
  saveSettings();
  sendStatus();
}

void handleCmd() {
  String d = server.arg("do");
  uint32_t now = millis();
  lastInteract = now;
  if      (d == "start" && phase == PH_IDLE) startFocus(now);
  else if (d == "pause")                     togglePause(now);
  else if (d == "stop")                      stopTimer(now);
  else if (d == "skip")                      skipPhase(now);
  sendStatus();
}

void handleMood() {
  String m = server.arg("m");
  uint32_t now = millis();
  lastInteract = now;
  for (const auto &e : MOOD_NAMES) {
    if (m == e.name) { setTimed(e.mood, 4000, now); break; }
  }
  sendStatus();
}

// ===== WIFI (non-blocking) =====================================
String wifiStatusText() {
  switch (WiFi.status()) {
    case WL_NO_SSID_AVAIL:   return "network not found";
    case WL_CONNECT_FAILED:  return "wrong password";
    case WL_CONNECTION_LOST: return "connection lost";
    case WL_DISCONNECTED:    return "disconnected";
    case WL_IDLE_STATUS:     return "starting";
    default:                 return "code " + String((int)WiFi.status());
  }
}

void wifiTask(uint32_t now) {
  static uint32_t lastTry = 0;
  static bool wasUp = false;

  bool up = WiFi.status() == WL_CONNECTED;
  if (up && !wasUp) {
    ipStr = WiFi.localIP().toString();
    mdnsUp = MDNS.begin(MDNS_NAME);
    if (!serverUp) {
      server.begin();
      serverUp = true;
    }
    Serial.print("Connected. Web UI: http://");
    Serial.println(ipStr);
  }
  if (!up && wasUp) ipStr = "";
  wasUp = up;

  if (!up && now - lastTry > 15000) {
    lastTry = now;
    Serial.print("WiFi: ");
    Serial.println(wifiStatusText());
    WiFi.disconnect(true);
    delay(100);
    WiFi.mode(WIFI_STA);
    WiFi.setTxPower(WIFI_POWER_11dBm);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
  }
}

// ===== SETUP & LOOP ============================================
void setup() {
  Serial.begin(115200);

  pinMode(TILT_PIN, INPUT_PULLUP);
  pinMode(TOUCH_PIN, INPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  attachInterrupt(digitalPinToInterrupt(TILT_PIN), onTilt, FALLING);

  Wire.begin(SDA_PIN, SCL_PIN);
  u8g2.begin();
  u8g2.setBusClock(400000);

  randomSeed(esp_random());
  loadSettings();

  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/set", HTTP_GET, handleSet);
  server.on("/api/cmd", HTTP_GET, handleCmd);
  server.on("/api/mood", HTTP_GET, handleMood);
  server.onNotFound([]() { server.send(404, "text/plain", "not found"); });

  uint32_t now = millis();
  lastInteract = now;
  nextBlinkAt = now + 2000;
  nextGlance  = now + 2000;
  nextWander  = now + 6000;
  cur = tgt = poseFor(M_CUTE, 0);
}

void loop() {
  uint32_t now = millis();

  wifiTask(now);
  if (serverUp) server.handleClient();

  pollTouch(now);
  handleShake(now);
  updateTimer(now);
  updateSound(now);

  if (mode == MODE_MENU) {
    if (now - menuTouchAt > MENU_IDLE_MS) {
      mode = MODE_EYES;
      lastInteract = now;
    } else if (now - lastFrame >= 60) {
      lastFrame = now;
      renderMenu();
    }
    return;
  }

  if (mode == MODE_IP) {
    if (now - ipShownAt > IP_SHOW_MS) {
      mode = MODE_EYES;
      lastInteract = now;
    } else if (now - lastFrame >= 200) {
      lastFrame = now;
      renderIp();
    }
    return;
  }

  Mood m = pickMood(now);
  if (m != mood) {
    if (m == M_SLEEP) play(SND_SLEEP);
    if (m == M_LOVE)  play(SND_LOVE);
    mood = m;
    onMoodEnter(m);
  }

  static uint32_t nextEmitAt = 0;
  if (now >= nextEmitAt) {
    uint32_t gap = 200;
    switch (mood) {
      case M_LOVE:
        spawnPart(P_HEART, 58 + random(0, 13), 40, -0.5f, 46);
        gap = 450;
        break;
      case M_HAPPY:
      case M_LAUGH:
        spawnPart(P_SPARK, random(0, 2) ? 8.0f : 118.0f, 8, 0, 10);
        gap = 600;
        break;
      case M_CRY:
        spawnPart(P_TEAR, LEFT_CX, 42, 0.5f, 12);
        spawnPart(P_TEAR, RIGHT_CX, 42, 0.5f, 12);
        gap = 300;
        break;
      case M_SLEEPY:
      case M_SLEEP:
        spawnPart(P_Z, 104, 16, -0.35f, 45);
        gap = 1400;
        break;
      default: break;
    }
    nextEmitAt = now + gap;
  }

  if (now - lastFrame < FRAME_MS) return;
  float dt = fminf((now - lastFrame) / 1000.0f, 0.1f);
  lastFrame = now;

  updateGlance(now);
  Pose t = poseFor(mood, now / 1000.0f);
  bool calm = (mood == M_SLEEP || mood == M_FOCUS || phase == PH_FOCUS);
  if (!calm) {
    t.gx += glanceX;
    t.gy += glanceY;
  }
  tgt = t;
  tween(1.0f - expf(-dt * 14.0f));

  float blink = (mood == M_SLEEP) ? 0.0f : blinkAmount(now);
  renderFace(now, blink);
}