# Desk Buddy — change notes

## v3.19 — OTA speed: radio tuned for weak-RF ESP32-C3 boards, no log-blocking during upload
**Why uploads took 30-40 minutes:** an ESP32-C3 SuperMini-type board with weak RF loses the ACKs it
sends back, so the PC's TCP keeps waiting and retransmitting. Plus, the espota path printed to the
USB serial port on every chunk, which blocks when no serial monitor is open.
**Now:** `WIFI_TX_POWER` and `WIFI_TX_POWER_OTA` = 8.5 dBm (the value reported most stable on these
boards; if the buddy will not join WiFi, set `WIFI_POWER_11dBm` again), 20 MHz channel width,
OTA progress printed every 10 % (espota) / 128 KB (browser) with RSSI and KB/s so the Serial Monitor
shows what the link is doing. Changed: `include/config.h`, `src/main.cpp`, `src/ota.cpp`.

## v3.18 — OTA upload no longer freezes the buddy; no power jump during upload
**What was wrong:** a browser/curl upload to `:8080/update` set `ota_busy` (pauses the whole app)
but never cleared it when the connection dropped mid-upload, so the buddy froze until a reboot and
hid the real failure. The upload also boosted the radio to 19.5 dBm, which drops the link or
browns out some ESP32-C3 boards (the same boards that only join WiFi at reduced power).

**Now:** `WIFI_TX_POWER_OTA` = 11 dBm (same as normal). An aborted upload resumes normal operation
at once; a web upload silent for 20 s is abandoned; every flash write is checked; Serial prints
`[OTA] web: N KB received` every 64 KB. Changed: `src/ota.cpp`, `include/config.h`.
**Needs one USB flash** (OTA itself was the broken part).

## v3.17 — idle sleep any time of day; back to sleep after an alarm
**What was wrong:** the "left alone, it falls asleep" behaviour only ever ran **inside the
do-not-disturb window** (web app -> Settings). Idle on the home face outside those hours, the
buddy just sat there with its face frozen (only the panel dimmed after 3 min). And an alarm that
woke it in the night left it standing awake on its home face afterwards.

**Now:**
- **Idle sleep works any time of day.** Untouched on the home face for 15 min it drifts to its
  sleeping face (with a little yawn), at 30 min total the panel goes dark. Any touch, picking it
  up, or any action in the web app (face change, style change, pet, party...) wakes it and
  restarts the count. Widgets, menus and study sessions are still never auto-slept.
- **Alarms and reminders wake it as before** (they always rang through), and **after the ring is
  answered/snoozed/ignored it drifts back to sleep by itself** - briefly showing the sleeping
  face (8 s) if the panel was already off, otherwise finishing its 15 min drift. An explicit
  wake (touch, web action) during the ring cancels the back-to-sleep.
- A sleep that starts **inside** the DND window still wakes up by itself when the window ends;
  a daytime nap sleeps until touched.

Details: the drift-off runs at the dimmed brightness (no full-glow face in a dark room). The
idle clock (`idleSince`, renamed from `dndIdle`) restarts on every interaction that already
restarted the DND clock, plus manual face/style choices. Config renames: `DND_IDLE_SLEEP_MS` ->
`IDLE_SLEEP_MS`, `DND_IDLE_DEEP_MS` -> `IDLE_DEEP_MS`, new `RING_RESLEEP_MS`. Changed:
`src/app.cpp`, `include/app.h`, `include/config.h`, README section 7, the web app's
do-not-disturb help text.

(v3.17 addendum, host side only - firmware identical) `tools\ota_update.bat` now
explains the one OTA failure the script cannot fix itself: "Authenticating ... OK" then
"No response from device" means the buddy's call-back to the PC was blocked (Windows
Firewall / VPN). The failure hint points at the browser update door on :8080, which needs
only PC-to-buddy traffic and always works. Same change documented in the README
troubleshooting section.

## v3.16 — OTA speed fix: 30-40 minute uploads were a software problem
**What was wrong:** wireless uploads crawled at ~1 KB/s (1.5 MB in 30-40 minutes). A healthy
Desk Buddy OTA runs at 100+ KB/s. Three software causes, all fixed:

1. **WiFi modem sleep.** By default the ESP32 radio sleeps between router beacons; every
   transfer then waits for wake-up windows, which can crush TCP to kilobytes per second.
   `wifiJoin()` now calls `WiFi.setSleep(false)` - the radio stays awake. The buddy is
   USB-powered, so the extra draw is irrelevant.
2. **OTA pause cadence.** While `ota_busy`, the main loop paused with `delay(20)`, letting the
   OTA handler drain the socket only ~50x/s (~50 KB/s ceiling). It now pauses with `delay(2)`,
   the same cadence as the normal loop (~500x/s).
3. **TX power during uploads.** `WIFI_TX_POWER` stays at the proven low value for joining
   (some ESP32-C3 boards only connect at reduced power), but while an upload runs the radio now
   temporarily boosts to the new `WIFI_TX_POWER_OTA` (full 19.5 dBm) - every lost TCP ACK
   throttles a transfer - and drops back afterwards. Joining is untouched.

Note: your internet plan speed has nothing to do with OTA - the firmware only travels
PC -> router -> buddy inside the LAN. Expected now: 1.5 MB in well under a minute (watch the
`[OTA] %` lines in the Serial Monitor). If it is still slow, it is the radio environment
(distance, 2.4 GHz congestion) - test with the buddy next to the router.

## v3.15 — OTA: flash over WiFi, USB cable retires
**What's new:** after one last USB flash, every future update goes over the air. Two doors, both
locked with the new `OTA_PASSWORD` (in `include/wifi_credentials.h`, git-ignored like the WiFi
credentials):

1. `tools\ota_update.bat` - builds with PlatformIO, then calls the espota uploader with the
   password. Optional argument: the buddy's IP (default `deskbuddy.local`).
2. Any browser: `http://deskbuddy.local:8080` (login `deskbuddy` / `OTA_PASSWORD`), upload
   `.pio\build\esp32c3\firmware.bin`. Works from a phone (use the IP on Android - it cannot
   resolve `.local`).

**Why one last USB flash was unavoidable:** OTA needs TWO app partitions to write into, and the
project was on `huge_app.csv` (one 3 MB app slot, no room for OTA). The layout is now
`min_spiffs.csv` (two ~1.9 MB app slots). A partition table can only be rewritten over USB - a
running image cannot repartition itself. Settings/alarms/tasks live in NVS at the same offset in
both layouts, so they survive the switch.

**Why the password is not in platformio.ini:** PlatformIO's espressif32 builder assembles the
espota command line itself (`--debug --progress -i <port>`) and ignores `upload_flags`, so there
is no ini option that could pass `--auth` - and a committed password would defeat its purpose
anyway. The .bat reads it from the git-ignored `wifi_credentials.h` at upload time. (Verified
against platform espressif32 v8.0.0's builder script and the bundled espota.py, which does
support `--auth`.)

**Safety:** an upload streams into the *inactive* app slot and only activates after arriving
complete - power loss mid-upload leaves the running firmware untouched. While an upload runs,
`ota_busy` pauses the buddy's app loop and the background weather fetch, so the radio is free.
The upload refuses to start (both doors) without the password.

**New files:** `include/ota.h`, `src/ota.cpp`, `tools/ota_update.bat`. Changed: `platformio.ini`
(partition layout + usage docs), `include/config.h` (OTA_PASSWORD guard), `include/wifi_credentials.h`
(third line), `src/main.cpp` (otaStart/otaTick + pause), `src/weather.cpp` (skips fetches during
update), README, this file.

## v3.14 — credentials file renamed wifi.h -> wifi_credentials.h (Windows build fix)
**What was wrong:** on Windows, file names are case-insensitive, and PlatformIO puts the
project's `include/` folder on the header search path before the framework's. A file named
`wifi.h` in `include/` then case-insensitively shadows the ESP32 library's own `<WiFi.h>`:
any file that includes `<WiFi.h>` got the credentials header instead, and the build failed with
a wall of "WiFiClass has not been declared" errors.
**Fix:** the file is now `include/wifi_credentials.h` (a name no library header uses), plus a
build-time `static_assert` that fails the compile - not the upload minutes later - while the
placeholder values are still set.

## v3.13 — credentials get their own file; web PIN retired; pomodoro jumps to the front
Credentials moved out of `secrets.h` into their own `include/wifi.h`, and `secrets.h` was
deleted - the optional web-app PIN (`API_PIN`) went with it: the buddy already sits behind your
WiFi password, so a second one bought little and cost a prompt in every browser tab. Also: a
pomodoro started from the web app now switches the buddy to its face screen (`appPomoToFront`)
- before, the timer ran invisibly if the buddy sat in widget or menu mode.

## v3.12 — credentials leave config.h (step 1: secrets.h)
WiFi name/password moved from the top of `include/config.h` into `include/secrets.h` (next to
the optional web PIN), so a screenshot of config.h no longer leaks them. The build refuses to
start until the file exists. (v3.11 was an intermediate cleanup step of the same move.)

## v3.10 — area-wise weather location search
The weather location search in the web app only knew **cities** (Open-Meteo geocoding),
so "Koramangala" or "HSR Layout" could not be pinned — you got Bengaluru's city-center
weather instead.

The search (Settings -> Clock & weather) now queries two sources and merges the results:

- **OpenStreetMap (Nominatim)** — the primary source. It indexes suburbs, neighbourhoods,
  quarters, localities, districts, villages, hamlets and towns, so an area resolves to
  its own coordinates. Each result is tagged with its type and shows its context
  ("Bengaluru, Bengaluru Urban, Karnataka, India · 12.91, 77.64").
- **Open-Meteo geocoding** — kept as a second source for plain city names. Entries that
  name the same place in the same spot are deduplicated (same name within ~0.5° counts
  as one; "Springfield" still lists all six states).

Picking a result saves a compact **"Area, City"** label (27 chars max, e.g.
"Indiranagar, Bengaluru") plus the area's exact lat/lon; the weather then refreshes
for that point. Both services are free and keyless; the search runs in the browser,
like before, so the phone/PC running the web app needs internet. Nothing changed on
the firmware side of the weather: it already fetched by coordinates.

Changed files: `tools/webui.html` (the search) and `src/webui.cpp` (regenerated with
`python3 tools/gen_webui.py`). Update: build and upload, then hard-refresh the web app.

Docs in the same release: the README gained a **Wiring and GPIO pinout** section (the
`PIN_*` defaults from `include/config.h`) and a full **expression gallery** - all 41 faces
plus the six eye styles as PNGs under `docs/faces/`, rendered with the same JS twin
(`src/webface.cpp`) that powers the web app's OLED mirror, so the thumbnails are
pixel-for-pixel what the buddy shows. `BUDDY_VERSION` now reports 3.10, and
`include/secrets.h.example` was added so a fresh clone builds (your real `secrets.h`
stays git-ignored). No firmware behaviour changed for any of this.

## v3.9 — web app fix

## What was wrong
The web app (hosted on the ESP32) stayed on "connecting…" forever, all home cards were
blank, and the eyes in the page's OLED mirror never adopted the buddy's actual face.

## Root cause: `/api/status` returned broken JSON
Two comma mistakes in `src/webapi.cpp` → `handleStatus()` made the status reply
invalid JSON, so the browser could never parse it:

1. The big `snprintf` ended with a trailing comma (`"alarms":%d,`) **and** the
   do-not-disturb block that is appended next starts with its own comma
   (`,"dnd":{...}`) → `"alarms":2,,"dnd":` — a double comma.
2. `addPomo()` appended `"pomodoro":{...}` with **no** leading comma →
   `..."active":false}"pomodoro":` — a missing comma.

With the reply unparseable, the page's first poll crashed before it could render
anything: header stuck on "connecting…", cards on their placeholders, and the mirror
canvas kept animating its locally-initialised neutral face instead of the buddy's
real face, style and gaze (those are only applied on a successful poll).

A third, smaller bug hid the failure: in `tools/webui.html` → `poll()`,
`offline=0` ran before the code that uses the reply, so the failure counter reset
to 0 and then went to 1 on **every** poll — it never reached the "offline ·
retrying…" threshold (3) and the app pretended to be connecting forever.

## The fix
- `src/webapi.cpp`
  - removed the trailing comma after `"alarms":%d`
  - `addPomo()` now prepends the comma (`,"pomodoro":{...}`), buffer 160 → 168
- `tools/webui.html` (and `src/webui.cpp`, regenerated with `python3 tools/gen_webui.py`)
  - `api()` now throws a clear error when a reply is not valid JSON instead of
    returning `null`
  - `poll()` only resets the offline counter after a status has fully rendered,
    so repeated failures correctly switch the header to "offline · retrying…"

All other JSON endpoints (alarms, tasks, settings, actions) were audited: they are
self-contained literals and were already correct.

## To update your buddy
Build and upload as usual (`pio run -t upload`), then reload the web app
(a hard refresh, Ctrl+Shift+R, is safest the first time). Settings, alarms and
tasks stored on the device are untouched.
