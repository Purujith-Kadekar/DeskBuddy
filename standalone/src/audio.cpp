#include "audio.h"
#include "config.h"
#include "faces.h"
#include <esp_arduino_version.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

// LEDC API differs between arduino-esp32 core 2.x (channel based)
// and 3.x (pin based) - support both.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
  #define BUZZ_ATTACH() ledcAttach(PIN_BUZZER, BUZZ_LEDC_HZ, BUZZ_LEDC_BITS)
  #define BUZZ_TONE(f)  ledcWriteTone(PIN_BUZZER, (uint32_t)(f))
  #define BUZZ_DUTY(d)  ledcWrite(PIN_BUZZER, (uint32_t)(d))
#else
  #define BUZZ_ATTACH() do { ledcSetup(BUZZ_LEDC_CH, BUZZ_LEDC_HZ, BUZZ_LEDC_BITS); \
                             ledcAttachPin(PIN_BUZZER, BUZZ_LEDC_CH); } while (0)
  #define BUZZ_TONE(f)  ledcWriteTone(BUZZ_LEDC_CH, (uint32_t)(f))
  #define BUZZ_DUTY(d)  ledcWrite(BUZZ_LEDC_CH, (uint32_t)(d))
#endif

// freq == 0 -> rest.  freq2 != 0 -> glide from freq to freq2 over the note.
struct Note { uint16_t freq; uint16_t ms; uint16_t freq2; };
#define T(f, ms)        { (f), (ms), 0 }
#define G(f0, f1, ms)   { (f0), (ms), (f1) }
#define R(ms)           { 0, (ms), 0 }

// ---------------- alarm / reminder melodies ----------------
static const Note M_CLASSIC[] PROGMEM = {
  T(880,80),R(80),T(880,80),R(80),T(880,80),R(80),T(660,140),R(220)
};
static const Note M_CHIME[] PROGMEM = {
  T(523,180),R(60),T(659,180),R(60),T(784,220),R(60),T(1047,340),R(640)
};
static const Note M_ROBOT[] PROGMEM = {
  T(988,60),R(40),T(988,60),R(40),T(1319,90),R(150),
  T(740,60),R(40),T(740,60),R(40),T(988,90),R(420)
};
static const Note M_SUNRISE[] PROGMEM = {
  T(392,120),T(523,120),T(659,120),T(784,180),R(90),T(659,100),T(784,320),R(520)
};
static const Note M_BIRD[] PROGMEM = {
  T(2093,60),R(30),T(2349,60),R(30),T(2093,60),R(140),
  T(1760,50),R(30),T(1976,50),R(320)
};
static const Note M_PING[] PROGMEM = {
  T(622,30),R(10),T(1245,420),R(780)
};
// reminder tones (gentler, repeat with a long gap)
static const Note M_NOTIFY[] PROGMEM = {
  T(1319,110),R(40),T(1568,110),R(40),T(2093,220),R(1100)
};
static const Note M_DROP[] PROGMEM = {
  G(2600,1700,120),R(70),G(1500,2300,130),R(1250)
};
static const Note M_PULSE[] PROGMEM = {
  T(1760,90),R(110),T(1760,90),R(110),T(2217,160),R(1150)
};

#define LEN(a) ((uint8_t)(sizeof(a) / sizeof(Note)))
struct Seq { const Note* n; uint8_t len; };
static const Seq MEL_TABLE[MEL_COUNT] = {
  {M_CLASSIC, LEN(M_CLASSIC)}, {M_CHIME, LEN(M_CHIME)}, {M_ROBOT, LEN(M_ROBOT)},
  {M_SUNRISE, LEN(M_SUNRISE)}, {M_BIRD, LEN(M_BIRD)},   {M_PING, LEN(M_PING)},
  {M_NOTIFY, LEN(M_NOTIFY)},   {M_DROP, LEN(M_DROP)},   {M_PULSE, LEN(M_PULSE)}
};

const char* const MELODY_NAMES[MEL_COUNT] = {
  "Classic Alarm", "Gentle Chime", "Robot Beeps", "Sunrise Arp", "Birdsong", "Radar Ping",
  "Tri-tone", "Water Drop", "Double Pulse"
};

// ---------------- UI sound effects (short, warm, quiet) ----------------
static const Note S_CLICK[]   PROGMEM = { T(587,18) };                          // soft tick
static const Note S_OK[]      PROGMEM = { T(523,60), R(20), T(659,90) };        // warm "mm-hm"
static const Note S_ERR[]     PROGMEM = { G(294,262,130), R(70), G(262,233,160) };
static const Note S_FANFARE[] PROGMEM = { T(523,80), T(587,80), T(659,80), T(784,90), T(1047,240) };  // pentatonic run
static const Note S_WAKE[]    PROGMEM = { G(392,523,120), R(40), G(523,659,170) };  // gentle good-morning
static const Note S_COO[]     PROGMEM = { G(659,523,150), R(40), G(523,440,200) };  // the "I love this" coo

// ---------------- contented murmurs while being petted ----------------
// NOT a continuous purr: the app asks for one of these every few seconds,
// a soft little mumble like a cat drifting off in your hand
static const Note S_MURMUR1[] PROGMEM = { G(330,392,120), R(90), G(392,330,160) };
static const Note S_MURMUR2[] PROGMEM = { G(392,330,200) };
static const Note S_MURMUR3[] PROGMEM = { T(392,60), R(70), T(330,90) };
static const Seq MURMURS[3] = {
  {S_MURMUR1, LEN(S_MURMUR1)}, {S_MURMUR2, LEN(S_MURMUR2)}, {S_MURMUR3, LEN(S_MURMUR3)}
};
static const Seq SFX_TABLE[6] = {
  {S_CLICK, LEN(S_CLICK)}, {S_OK, LEN(S_OK)}, {S_ERR, LEN(S_ERR)},
  {S_FANFARE, LEN(S_FANFARE)}, {S_WAKE, LEN(S_WAKE)}, {S_COO, LEN(S_COO)}
};

// ---------------- voices: only the expressive few ----------------
// A pet that chirps on every face change is an irritating bot. Only the
// strongly expressive moments speak, and softly: short phrases from the warm
// C-major pentatonic range (262-880 Hz), never the harsh 2-3 kHz buzzer
// region. Everything else maps to E_SILENCE below.
// (emo_table.inc references an E_<NAME> symbol for every expression in
//  tools/gen_faces.py - the silent ones are #defined to E_SILENCE so the
//  generated table keeps compiling unchanged.)
static const Note E_HAPPY[]     PROGMEM = { G(392,523,90), R(30), T(587,70) };             // little "hi!"
static const Note E_LOVE[]      PROGMEM = { G(659,523,160), R(40), G(523,392,220) };      // dreamy coo
static const Note E_SURPRISED[] PROGMEM = { G(330,587,110) };                             // a soft "oh?"
static const Note E_ANGRY[]     PROGMEM = { G(330,262,90), R(50), T(262,110) };           // low "hmph"
static const Note E_SAD[]       PROGMEM = { G(440,330,240) };                             // a sigh
static const Note E_CRY[]       PROGMEM = { G(440,392,110), R(70), G(330,294,200) };      // gentle sob
static const Note E_SLEEPY[]    PROGMEM = { G(392,262,380) };                             // yawn
static const Note E_SLEEP[]     PROGMEM = { G(392,294,280), R(120), G(294,262,320) };     // drifting off
static const Note E_YAWN[]      PROGMEM = { G(440,330,300), R(60), G(330,262,260) };      // yaaawn
static const Note E_PARTY[]     PROGMEM = { T(523,70), T(587,70), T(659,70), T(784,180) };// quick yay
static const Note E_WOW[]       PROGMEM = { G(330,784,200), R(30), T(880,90) };           // soft wow
static const Note E_CUTE[]      PROGMEM = { T(659,60), T(784,90) };                       // twinkle
static const Note E_KISS[]      PROGMEM = { G(659,880,70), R(20), T(784,50) };            // mwah
static const Note E_ANIMELOVE[] PROGMEM = { T(523,60), T(659,60), T(784,110) };           // sparkly
// every other expression is silent - a face change alone is expression enough
static const Note E_SILENCE[] PROGMEM = { R(1) };
#define E_NEUTRAL    E_SILENCE
#define E_LAUGH      E_SILENCE
#define E_WINK       E_SILENCE
#define E_SHY        E_SILENCE
#define E_FOCUS      E_SILENCE
#define E_CHILL      E_SILENCE
#define E_DIZZY      E_SILENCE
#define E_BORED      E_SILENCE
#define E_CONFUSED   E_SILENCE
#define E_PANIC      E_SILENCE
#define E_SMUG       E_SILENCE
#define E_SUS        E_SILENCE
#define E_THINKING   E_SILENCE
#define E_DEAD       E_SILENCE
#define E_SERIOUS    E_SILENCE
#define E_BAWL       E_SILENCE
#define E_MINILOVE   E_SILENCE
#define E_FIRE       E_SILENCE
#define E_BLEP       E_SILENCE
#define E_BLISS      E_SILENCE
#define E_GIGGLE     E_SILENCE
#define E_POUT       E_SILENCE
#define E_NERVOUS    E_SILENCE
#define E_DREAMY     E_SILENCE
#define E_MISCHIEF   E_SILENCE
#define E_XD         E_SILENCE
#define E_WHISTLE    E_SILENCE
#include "emo_table.inc"

// ---------------- notification intro: vibrate-buzz, then a "ding" ----------------
static const Note N_INTRO[] PROGMEM = {
  T(260,55),R(35),T(260,55),R(35),T(260,55),R(35),T(260,55),R(90),
  T(1568,80), T(2093,170), R(450)
};

// ---------------- player (RAM buffer so sequences can be composed) ----------------
enum Prio : uint8_t { PR_EMO = 0, PR_SFX = 1, PR_CUCKOO = 2, PR_ALARM = 3 };
#define BUF_N 64
static Note buf[BUF_N];
static uint8_t bufLen = 0, loopFrom = 0, idx = 0;
static bool    looping = false, playing = false;
static uint8_t curPrio = PR_EMO;
static uint32_t noteStart = 0, noteEnd = 0;
static uint16_t lastF = 0;
static uint8_t  vol = 6;
static bool     softMode = false, softNext = false;   // murmurs play extra soft
static SemaphoreHandle_t lock = nullptr;

static inline void LOCK()   { if (lock) xSemaphoreTake(lock, portMAX_DELAY); }
static inline void UNLOCK() { if (lock) xSemaphoreGive(lock); }

// loudness at 0..1023 LEDC duty: EVERY class (emotions, clicks, cuckoo,
// alarms) plays at the FULL set volume - the volume slider is the only
// volume control. Only the petting murmurs stay soft.
// "Sound: off" mutes the personality, never the alarm: an alarm / reminder that is
// sounding plays at a medium level even when the volume is 0, so you cannot oversleep.
static inline uint8_t curVol() { return (vol == 0 && playing && curPrio >= PR_ALARM) ? 6 : vol; }

static uint32_t targetDuty() {
  uint32_t d = (uint32_t)curVol() * 51UL;             // 0..510 of 1023 (50% max)
  if (softMode) d = d * 11 / 20;                      // a murmur, not a demand
  if (d < 40) d = 40;
  return d;
}

static void toneOut(uint16_t freq) {
  lastF = freq;
  if (freq == 0 || curVol() == 0) { BUZZ_TONE(0); return; }
  BUZZ_TONE(freq);                                  // WriteTone resets duty -> set volume after it
  BUZZ_DUTY(targetDuty());
}

static void startNote() {
  Note& n = buf[idx];
  noteStart = millis();
  noteEnd = noteStart + n.ms;
  toneOut(n.freq);
}

static void silence() {
  playing = false; looping = false; bufLen = 0; softMode = false;
  BUZZ_TONE(0);
}

// append a PROGMEM sequence to the RAM buffer
static void append(const Note* src, uint8_t len) {
  for (uint8_t i = 0; i < len && bufLen < BUF_N; i++) memcpy_P(&buf[bufLen++], &src[i], sizeof(Note));
}

// begin playing whatever is in buf (caller holds the lock)
static bool begin(uint8_t prio, bool loop, uint8_t loopStart) {
  if (playing && prio < curPrio) return false;      // never cut off something more important
  curPrio = prio;
  softMode = softNext; softNext = false;
  looping = loop; loopFrom = loopStart;
  idx = 0; playing = true;
  startNote();
  return true;
}

static void step() {
  if (!playing) return;
  uint32_t now = millis();
  if ((int32_t)(now - noteEnd) >= 0) {
    idx++;
    if (idx >= bufLen) {
      if (looping) idx = loopFrom;
      else { silence(); return; }
    }
    startNote();
    return;
  }
  const Note& n = buf[idx];
  if (n.freq && n.freq2) {                          // glide
    uint32_t el = now - noteStart;
    int f = (int)n.freq + (int)(((int32_t)n.freq2 - (int32_t)n.freq) * (int32_t)el / (int32_t)n.ms);
    if (abs(f - (int)lastF) >= 12) toneOut((uint16_t)f);
  }
}

static void audioTask(void*) {
  for (;;) {
    LOCK(); step(); UNLOCK();
    vTaskDelay(pdMS_TO_TICKS(4) > 0 ? pdMS_TO_TICKS(4) : 1);
  }
}

// ---------------- public API ----------------
void audioBegin(uint8_t volume10) {
  BUZZ_ATTACH();
  BUZZ_TONE(0);
  vol = (volume10 > 10) ? 10 : volume10;
  lock = xSemaphoreCreateMutex();
  xTaskCreate(audioTask, "audio", 4096, nullptr, 2, nullptr);
}

void audioSetVolume(uint8_t v10) { vol = (v10 > 10) ? 10 : v10; }
uint8_t audioVolume() { return vol; }

void audioPlay(MelodyId m, bool loop) {
  if (m >= MEL_COUNT) m = MEL_CLASSIC;
  LOCK();
  bufLen = 0;
  append(MEL_TABLE[m].n, MEL_TABLE[m].len);
  begin(PR_ALARM, loop, 0);
  UNLOCK();
}

void audioPlayNotify(MelodyId m) {
  if (m < MEL_FIRST_NOTIFY || m >= MEL_COUNT) m = MEL_NOTIFY;
  LOCK();
  bufLen = 0;
  append(N_INTRO, LEN(N_INTRO));
  uint8_t start = bufLen;                           // from here on the tone repeats
  append(MEL_TABLE[m].n, MEL_TABLE[m].len);
  begin(PR_ALARM, true, start);
  UNLOCK();
}

void audioPlaySfx(SfxId s) {
  if (s > SFX_COO) return;
  LOCK();
  if (!(playing && PR_SFX < curPrio)) {
    bufLen = 0;
    append(SFX_TABLE[s].n, SFX_TABLE[s].len);
    begin(PR_SFX, false, 0);
  }
  UNLOCK();
}

void audioPlayEmotion(uint8_t e) {
  if (e >= EX_COUNT) return;
  LOCK();
  if (!(playing && PR_EMO < curPrio)) {
    bufLen = 0;
    append(EMO_TABLE[e].n, EMO_TABLE[e].len);
    begin(PR_EMO, false, 0);
  }
  UNLOCK();
}

// cu-ckoo, cu-ckoo ... like an old wall clock
void audioCuckoo(uint8_t count) {
  if (count < 1) count = 1;
  if (count > 12) count = 12;
  LOCK();
  if (!(playing && PR_CUCKOO < curPrio)) {
    bufLen = 0;
    for (uint8_t i = 0; i < count && bufLen + 4 <= BUF_N; i++) {
      buf[bufLen++] = Note T(1568, 170);            // "cu"  (high)
      buf[bufLen++] = Note R(25);
      buf[bufLen++] = Note T(1245, 300);            // "ckoo" (a minor third lower)
      buf[bufLen++] = Note R((uint16_t)(i + 1 < count ? 430 : 60));
    }
    begin(PR_CUCKOO, false, 0);
  }
  UNLOCK();
}

void audioPurr() {      // one soft contented murmur; the app spaces them a few seconds apart
  const Seq& s = MURMURS[esp_random() % 3];
  LOCK();
  if (!playing) {
    bufLen = 0;
    append(s.n, s.len);
    softNext = true;
    begin(PR_SFX, false, 0);
  }
  UNLOCK();
}

void audioStopPurr() {
  LOCK();
  if (playing && softMode) silence();
  UNLOCK();
}

void audioStop() {
  LOCK();
  silence();
  UNLOCK();
}

void audioTick() {}                                 // audio runs in its own task now

bool audioBusy() { return playing; }
bool audioAlarmBusy() { return playing && curPrio >= PR_CUCKOO; }
