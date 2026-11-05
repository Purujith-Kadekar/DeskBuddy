#pragma once
#include <Arduino.h>

// ============================================================
//  audio.h - buzzer engine (LEDC driven, runs in its own task)
//
//  * pitch slides, so sighs / chirps / "wah-wah" sound natural
//  * soft attack / release envelopes: notes swell in and fade
//    out instead of buzzing in with a click
//  * volume tiers: personality voices are quiet, alarms and
//    reminders stay at full strength - and still sound (medium)
//    when the volume is set to 0
//  * priorities: emotion < sfx < cuckoo < alarm/notification
//    (a lower priority sound never cuts off a higher one)
//  * only the expressive few faces have a voice - most face
//    changes are silent (audioPlayEmotion)
//  * cuckoo clock + phone-style notification (buzz + ding, then
//    a tone that repeats until audioStop())
// ============================================================

enum MelodyId : uint8_t {
  MEL_CLASSIC,   // urgent hi-lo beeps          (alarm)
  MEL_CHIME,     // soft rising chime           (alarm)
  MEL_ROBOT,     // robotic morse               (alarm)
  MEL_SUNRISE,   // ascending arp               (alarm)
  MEL_BIRD,      // chirpy bird                 (alarm)
  MEL_PING,      // sonar ping                  (alarm)
  MEL_NOTIFY,    // tri-tone                    (reminder)
  MEL_DROP,      // water drop                  (reminder)
  MEL_PULSE,     // double pulse                (reminder)
  MEL_COUNT
};
#define MEL_FIRST_NOTIFY MEL_NOTIFY

enum SfxId : uint8_t { SFX_CLICK = 0, SFX_OK, SFX_ERR, SFX_FANFARE, SFX_WAKE, SFX_COO };

extern const char* const MELODY_NAMES[MEL_COUNT];

void audioBegin(uint8_t volume10);
void audioSetVolume(uint8_t volume10);   // 0..10
uint8_t audioVolume();

void audioPlay(MelodyId m, bool loop);        // alarm-priority melody
void audioPlayNotify(MelodyId m);             // phone notification: buzz + ding, then repeats until stopped
void audioPlaySfx(SfxId s);
void audioPlayEmotion(uint8_t expression);    // index = Expression enum (faces.h)
void audioPurr();                             // one soft contented murmur (petting; the app spaces them out)
void audioStopPurr();                         // stops it only if a purr is playing
void audioCuckoo(uint8_t count);              // cuckoo clock, `count` cuckoos (1..12)
void audioStop();
void audioTick();                             // kept for compatibility (audio runs in its own task)
bool audioBusy();
bool audioAlarmBusy();                        // an alarm / reminder / cuckoo is sounding
