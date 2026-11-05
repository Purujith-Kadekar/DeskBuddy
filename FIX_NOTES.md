# Desk Buddy — change notes

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
