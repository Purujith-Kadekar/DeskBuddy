#pragma once
// ============================================================
//  config.h - everything you might want to change, in one place
//
//  >>> PUT YOUR WIFI NAME AND PASSWORD RIGHT HERE (2.4 GHz only) <<<
//  The optional web PIN is in include/secrets.h.
// ============================================================
#include "secrets.h"

// ---------------- WiFi (2.4 GHz only) ----------------
#define WIFI_SSID           "YourWiFiName"
#define WIFI_PASS           "YourWiFiPassword"
// reduced TX power (the value your board joined with on v3.5)
#define WIFI_TX_POWER       WIFI_POWER_11dBm
#define WIFI_RETRY_MS       15000         // re-join attempt every 15 s
#define MDNS_HOST           "deskbuddy"   // http://deskbuddy.local

// ---------------- identity ----------------
#define BUDDY_VERSION       "3.10"
#define BUDDY_NAME_DEF      "Buddy"

// ---------------- time ----------------
#define TZ_DEFAULT          "IST-5:30"    // POSIX timezone (India)
#define NTP_SERVER_1        "time.nplindia.in"   // NPL India
#define NTP_SERVER_2        "time.nic.in"        // NIC India
#define NTP_SERVER_3        "pool.ntp.org"       // third safety server
// if India's servers stay silent, alternate between the global pools
#define NTP_FALLBACK_1      "pool.ntp.org"
#define NTP_FALLBACK_2      "time.google.com"
#define NTP_FALLBACK_AFTER_S 45
#define NTP_RESYNC_S        3600          // hourly refresh

// ---------------- pins (match YOUR wiring) ----------------
// !! match these to YOUR wiring, or the display stays black !!
#define PIN_OLED_SDA        4             // I2C data  to SH1106
#define PIN_OLED_SCL        5             // I2C clock to SH1106
#define PIN_TOUCH           1             // TTP223 touch output (HIGH = touched)
#define PIN_TILT            3             // tilt switch (other leg to GND)
#define PIN_BUZZER          2             // passive buzzer

// ---------------- touch tuning ----------------
#define TOUCH_ACTIVE_LOW    0             // TTP223: 1 if touched = LOW
#define TOUCH_DEBOUNCE_MS   25            // level must stay steady this long
#define TOUCH_MIN_MS        30            // glitches shorter than this are dropped
#define TOUCH_HOLD_MS       600           // hold fires at 600 ms while down
#define TOUCH_DOUBLE_MS     350           // 2nd press inside this = double tap
#define TOUCH_MULTI_GAP_MS  280           // quiet gap that resolves tap counts (lower = snappier taps)
#define TOUCH_PET_HOLD_MS   450           // touch longer than this = petting
#define TOUCH_SCRATCH_GAP_MS 250          // back-to-back contacts this close chain
#define TOUCH_SCRATCH_N     4             // chained contacts that mean scratching
#define TOUCH_SCRATCH_WIN_MS 2000         // scratch-rate window
#define TOUCH_PET_END_MS    1400          // quiet time that ends petting
// TTP223 jumper set to TOGGLE (latching) mode? Then set TOUCH_TOGGLE_MODE 1.
// (Auto-detecting it from a long press was removed: a hand resting on the buddy
//  for 90 s used to flip it into toggle mode until the next reboot.)
#define TOUCH_TOGGLE_MODE   0
#define TOUCH_LATCH_MS      0             // 0 = never auto-detect (recommended)

// ---------------- buzzer (LEDC) ----------------
#define BUZZ_LEDC_HZ        2000
#define BUZZ_LEDC_BITS      10
#define BUZZ_LEDC_CH        0             // arduino-esp32 2.x only

// ---------------- behaviour ----------------
#define MAX_ALARMS          32            // alarms + reminders stored in NVS
#define MAX_TASKS           32            // calendar tasks stored in NVS
#define SNOOZE_MINUTES      9
#define CUCKOO_FIRST_HOUR   7             // cuckoo window 07:00 - 23:00
#define CUCKOO_LAST_HOUR    23
#define QUIET_START_HOUR    23            // emotion sounds muted 23:00 - 07:00
#define QUIET_END_HOUR      7
// do-not-disturb (web app -> Settings): inside the window the buddy is silent
// and, left alone on the home face, drifts to its sleeping face, then powers
// the panel down. Alarms and reminders always ring through.
#define DND_IDLE_SLEEP_MS   (15UL*60UL*1000UL)  // untouched on the home face -> sleeping face
#define DND_IDLE_DEEP_MS    (15UL*60UL*1000UL)  // sleeping face -> panel off (30 min total)
// ---------------- tilt switch (emotions when picked up / tipped / rocked) ----------------
#define TILT_DEBOUNCE_MS    60            // a ball switch rattles: level must hold this long
#define TILT_REHOME_MS      90000UL       // tipped this long = that is the new "upright"
#define TILT_T_CURIOUS_MS   400           // tipped this long -> curious / confused
#define TILT_T_UNEASY_MS    2500          //   ...        -> dizzy / uneasy
#define TILT_T_UPSET_MS     8000          //   ...        -> upset
#define TILT_T_CRY_MS       20000         //   ...        -> crying
#define TILT_RELIEF_MS      2500          // tipped at least this long -> "phew" when set down
#define TILT_ROCK_N         4             // direction changes within TILT_ROCK_WIN_MS = rocking
#define TILT_ROCK_WILD_N    7             //   ...more than this = rocking wildly (dizzy)
#define TILT_ROCK_WIN_MS    5000
#define TILT_WAKE_MS        1500          // held tipped this long wakes a sleeping buddy (not in DND)

// ---------------- OLED care (software only) ----------------
// after this long with no touch / tilt / alarm the panel dims (extends OLED life, less glare at night);
// any interaction brings it back to the brightness you set
#define OLED_DIM_AFTER_MS   (3UL*60UL*1000UL)
#define OLED_DIM_PERCENT    30            // dimmed level, % of your brightness (never below 40/255)

// ---------------- ringing limits ----------------
#define RING_ALARM_MS       120000UL      // an unanswered alarm stops after 2 min and snoozes...
#define RING_REMINDER_MS    60000UL       // ...a reminder after 1 min
#define RING_AUTO_SNOOZES   2             // ...this many times, then it gives up
#define MISSED_ALARM_MS     (30UL*60UL*1000UL)  // one-time alarms missed during a power cut ring on boot if <= 30 min late
#define PET_REACT_MS        1500
#define DIZZY_MS            4000
#define FRAME_MS            33            // ~30 fps animation loop
