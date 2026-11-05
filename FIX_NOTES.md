# Desk Buddy — change notes

## v3.14 — credentials file renamed wifi.h -> wifi_credentials.h (Windows build fix)
**What was wrong:** on Windows, file names are case-insensitive, and PlatformIO puts the
project's `include/` folder on the header search path. So the moment `include/wifi.h`
existed, every `#include <WiFi.h>` in the firmware (main, webapi, weather, clocksync,
app, widgets - six files) resolved to **your credentials file** instead of the ESP32
WiFi library. The build then failed with `'WiFi' was not declared`, `'WL_CONNECTED' was
not declared`, `'WIFI_STA' was not declared; did you mean WIFI_SSID?` and
`'WIFI_POWER_11dBm' was not declared`. That "did you mean WIFI_SSID?" was the tell:
WIFI_SSID is your own macro. Filling in the credentials correctly changed nothing -
the file's mere existence under that name was the problem. Linux and macOS are
case-sensitive, so the same tree built fine there, which is why this slipped through.

**Fix:** the credentials file is now **include/wifi_credentials.h** - no ESP32/Arduino
header carries that name, so `<WiFi.h>` resolves to the real library on every
filesystem. `config.h` pulls the credentials from it; `.gitignore` excludes it (and
still excludes the old `wifi.h` name, in case a v3.13 copy lingers).

**Upgrading from v3.13:** rename `include/wifi.h` to `include/wifi_credentials.h`
(credentials inside unchanged) and rebuild. If you copy the v3.14 files over an
existing project, delete any leftover `include/wifi.h` afterwards - as long as that
file exists, it still shadows `<WiFi.h>` on Windows.

Changed files: `include/wifi_credentials.h` (renamed), `include/config.h`,
`src/main.cpp` (messages), `.gitignore`, README, this file. Update: rename the file,
build and upload.

## v3.13 — one simple credentials file (include/wifi.h), web-app PIN removed
**What changed:** your WiFi name and password now live in exactly one place,
**include/wifi.h** - two lines, nothing else - and that file is in `.gitignore`, so it is
never committed. `config.h` pulls the credentials from it (`#include "wifi.h"`); all the
WiFi tuning values stay in `config.h` as before.

The optional web-app **PIN feature is gone entirely**: no `API_PIN`, no `X-Pin` header,
no PIN prompt in the web app, no `include/secrets.h`, no `secrets.h.example`. Anyone on
your WiFi can open the web app, exactly like before the PIN existed. If you had a
`secrets.h`, move its two WiFi lines into `wifi.h` and delete it.

The build still refuses to run until the credentials are real: no `include/wifi.h` ->
*"include/wifi.h not found: create it with your WiFi name/password - see README section 1"*;
placeholders left in -> the `static_assert` stops the build, pointing at `include/wifi.h`.

Changed files: `include/wifi.h` (new), `include/config.h`, `src/main.cpp` (messages),
`src/webapi.cpp` (auth removed), `tools/webui.html` + regenerated `src/webui.cpp`
(PIN prompt removed), `.gitignore`, `include/secrets.h.example` (deleted), README, this
file. Update: move your credentials into `include/wifi.h`, build and upload.

## v3.12 — WiFi credentials moved out of config.h
**Why:** `WIFI_SSID` / `WIFI_PASS` used to sit in `include/config.h`, a file you commit -
one careless `git add .` away from publishing your WiFi password. They now live in
**include/secrets.h**, which `.gitignore` has always excluded, next to the optional
web-app PIN. The committed template is **include/secrets.h.example**: copy it to
`include/secrets.h`, fill in your WiFi name/password, build.

Nothing else about WiFi changed - `main.cpp` reads the same macros through `config.h`,
and the tuning values (TX power, retry interval, mDNS name) stay in `config.h` because
they are not secret.

Two guard rails make the move impossible to get wrong:

- no `include/secrets.h` at all -> the build stops with
  *"WIFI_SSID is missing: copy include/secrets.h.example to include/secrets.h and set
  your WiFi name/password there"*;
- the file exists but still holds the placeholder -> the existing `static_assert`
  stops the build, now pointing at `include/secrets.h` too. The Serial Monitor hint
  was updated to match.

**Upgrading:** take your WiFi name/password out of `include/config.h` and put them in
`include/secrets.h` (create it from the example if it does not exist). If you ever
pushed an older `config.h` with real credentials, remember git keeps them in history -
rotate the WiFi password or rewrite the history before making the repo public.

Changed files: `include/config.h`, `include/secrets.h.example`, `src/main.cpp` (two
messages), README, this file. Update: move your credentials, build and upload.

## v3.11 — study timer started from the web app now shows up
**What was wrong:** starting the pomodoro from the web app while the buddy was on a
widget screen left it there. The timer ran (the corner of the widget screen carried a
small "STUDY 24:59"), but the actual focus experience - the big focus face with study
glasses, the countdown and the progress bar - only ever appeared on the home face
screen, and only if you happened to be on it. The device menu did it right: its
Timer item switches to the face screen after starting. The web API's `start` just
called `pomoStart()` and left the screen alone.

**The fix:** new `appPomoToFront()` in `src/app.cpp` - the session owns the face, so a
start (or resume) that comes from the web app now leaves the widget screens (and a
dark panel: it wakes the buddy) and lands on the face screen, exactly like the menu's
own start. `handlePomodoro()` in `src/webapi.cpp` calls it for the `start` action,
but only when the action actually begins or resumes a session - pressing start while
a session is already live no longer yanks the screen. A ringing alarm keeps priority;
the session simply continues behind it. Pausing, skipping and resetting are unchanged
(no screen change).

Verified with a host-compiled harness running the exact transition logic: start /
resume on widgets -> face screen; no-op start while live -> stays on widgets; start on
face / while ringing / while the menu is open / while the panel is asleep all behave
as described above.

Also in this release: `.gitignore` now covers all PlatformIO build output
(`.pio/`, legacy dirs, `compile_commands.json`, editor-generated files, stray
`*.o/*.elf/...`), and `BUDDY_VERSION` reports 3.11.

Changed files: `src/app.cpp`, `include/app.h`, `src/webapi.cpp`, `.gitignore`,
`include/config.h`, this file, README. Update: build and upload.

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
