#pragma once
// ============================================================
//  config.h - everything you might want to change, in one place
//
//  >>> YOUR WIFI NAME AND PASSWORD LIVE IN include/wifi_credentials.h <<<
//  (that file is git-ignored, so they never reach GitHub)
// ============================================================
#if !__has_include("wifi_credentials.h")
#error "include/wifi_credentials.h not found: create it with your WiFi name/password - see README section 1"
#endif
#include "wifi_credentials.h"

#ifndef WIFI_SSID
#error "WIFI_SSID is missing: set it in include/wifi_credentials.h"
#endif
#ifndef WIFI_PASS
#error "WIFI_PASS is missing: set it next to WIFI_SSID in include/wifi_credentials.h"
#endif

// -------- OTA (wireless flashing) - the password lives in wifi_credentials.h --------
#ifndef OTA_PASSWORD
#error "OTA_PASSWORD is missing: add  #define OTA_PASSWORD \"YourChosenPassword\"  to include/wifi_credentials.h"
#endif

// ---------------- WiFi tuning (the credentials are in wifi_credentials.h) ----------------
// reduced TX power (the value your board joined with on v3.5 - some ESP32-C3
// boards only join like this). OTA uploads temporarily boost to the OTA value.
#define WIFI_TX_POWER       WIFI_POWER_8_5dBm   // (v3.19) 8.5 dBm = the value reported most stable on ESP32-C3 SuperMini-type boards
                                                // (weak RF/regulator: higher power = lost ACKs = crawling uploads). Won't join? try WIFI_POWER_11dBm
#define WIFI_TX_POWER_OTA   WIFI_POWER_8_5dBm   // same as normal: a power jump during upload drops the link on these boards
#define WIFI_RETRY_MS       15000         // re-join attempt every 15 s
#define MDNS_HOST           "deskbuddy"   // http://deskbuddy.local

// ---------------- identity ----------------
#define BUDDY_VERSION       "3.19"
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
// do-not-disturb (web app -> Settings): inside the window the buddy is silent.
// Idle sleep is separate and works ANY time of day: untouched on its home face
// it drifts to its sleeping face, then powers the panel down. Alarms and
// reminders always ring through, wake it, and after the ring it drifts back
// to sleep by itself. Widgets, menus and study sessions never auto-sleep.
#define IDLE_SLEEP_MS       (15UL*60UL*1000UL)  // untouched on the home face -> sleeping face
#define IDLE_DEEP_MS        (15UL*60UL*1000UL)  // sleeping face -> panel off (30 min total)
#define RING_RESLEEP_MS     8000UL              // woken from panel-off sleep by a ring:
                                                // show the sleeping face this long, then dark again
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
