# Planner Sync Architecture

Cloud-first planner (tasks, reminders, alarms) for a two-device workspace:
the **DeskBuddy** (ESP32-C3 desk companion) and the **Ventuno Q**. Vercel is
the single source of truth; both devices hold a full local copy so alarms
ring even with Wi-Fi down.

```
                 ┌─────────────────────────────┐
                 │   Vercel (Next.js, JS)      │
                 │   /            planner UI   │
                 │   /api/sync    GET | POST   │
                 │   lib/store.js             │
                 │     ├─ mock store (default) │
                 │     └─ Firestore            │
                 │        planner/main (1 doc) │
                 └──────────┬──────────────────┘
          poll 30s / push   │   poll 30s / push
        ┌───────────────────┴───────────────────┐
        │                                         │
 ┌──────┴────────┐                       ┌────────┴──────┐
 │  DeskBuddy    │  HTTP + NTP clock    │  Ventuno Q    │
 │  (ESP32-C3)   │  local NVS mirror    │  (client)     │
 │  alarms ring  │  alarm engine runs   │  same protocol│
 │  locally      │  100 % locally       │               │
 └───────────────┘                      └───────────────┘
```

## The document

One Firestore document holds everything:

```
collection "planner", document "main"
{
  rev: 42,                  // bumped on every accepted change
  tasks: [ { id, title, date, end, time, done }, ... ],
  alarms: [ { id, enabled, type, kind, time, date, weekdays, melody, label }, ... ]
}
```

- `id` is `V` (server-created) or `L` (device-created) + up to 11 lowercase hex chars,
  `L` + 11 hex for device-created ones. The prefixes make collisions
  between the two sides impossible without coordination.
- `alarm.type` is `alarm` (full-screen ring) or `reminder` (notification).
- `alarm.kind` is `once` | `daily` | `weekly` (Mon-first bitmask in
  `weekdays`) | `countdown`. The server stores countdowns resolved to an
  absolute `date` + `time` (the ringing device's clock is authoritative).
- `task.end` (optional) lets a task span days. `task.time` is ordering-only
  and never rings.

## The protocol

### `GET /api/sync?dev=<name>&rev=<n>`

| reply | meaning | Firestore cost |
| --- | --- | --- |
| `200 {ok, rev, tasks, alarms}` | the client is behind (or never wrote) | **1 read** |
| `304` | nothing changed since `rev` | **1 read** (the rev is compared server-side; only the payload is saved) |

A never-written document (rev 0, empty arrays) always answers `200`, so a
fresh device booting at `rev=0` sees the empty state and can seed it.

### `POST /api/sync` — `{ dev, baseRev, ops: [...] }`

```json
{ "dev": "deskbuddy", "baseRev": 41,
  "ops": [
    { "op": "alarm.put", "id": "labc123def45", "alarm": { ... } },
    { "op": "alarm.del", "id": "lfed654cba32" },
    { "op": "task.put",  "id": "l11122233344", "task":  { ... } }
  ]
}
```

| reply | meaning | Firestore cost |
| --- | --- | --- |
| `200 {ok, rev, tasks, alarms}` | ops applied, whole doc returned | **1 read + 1 write** |
| `409 {ok:false, conflict, rev, tasks, alarms}` | stale `baseRev` | **1 read, 0 writes** |

The store runs the apply inside a **Firestore transaction**: read the doc,
check `baseRev`, apply all ops to the in-memory copy, write the **whole
document** once. A partial write is impossible by construction, and a batch
of ops costs the same single write as one op. An op whose content equals
what is stored is a no-op: no write, no rev bump (idempotent retries are
free). On 409 the client adopts the returned state and re-sends its ops on
the new rev — one round-trip, still no lost writes.

Validation happens before anything is stored: dates must be real calendar
dates, `end` >= `date`, times well-formed, labels sanitized to printable
ASCII (the OLED fonts cannot draw more), ids well-formed. A fully-invalid
op set changes nothing and does not bump the rev.

### Auth

Set `SYNC_API_KEY` (env var) and clients send it as `X-Api-Key`. The
DeskBuddy sends the same value from `CLOUD_API_KEY` in its
`wifi_credentials.h`. Without the env var the API is open (local dev).

## Firestore cost discipline (why this design)

The requirement: **one read or one write should do the whole job; updates
must never be partial or multi-write.**

| action | reads | writes |
| --- | --- | --- |
| device poll, nothing new (304) | 1 (rev check; saves bandwidth only) | 0 |
| device poll, something changed | 1 | 0 |
| any edit (1 op or 12 ops) | 1 | 1 |
| conflicting edit (409 + retry) | 2 | 1 |

At the default 30 s poll with two devices: ≈ 4 reads/minute when idle
(each poll reads the doc once to compare `rev`), plus 2 quota units per actual change. The
whole-document design also means there is no document fan-out, no
per-entry documents, and no multi-document transactions to get wrong.

## The DeskBuddy side (firmware v3.20)

- `src/cloudsync.cpp` — the client twin of this protocol (background task,
  same recipe as the weather fetcher). Polls every 30 s, pushes op batches
  ~20 s after a local change (batching window), retries with backoff.
- **Seeding**: on first contact, if the cloud doc is empty and the device
  has local entries, it uploads them as one `alarm.put`/`task.put` per
  entry in a single POST. Nothing is lost when moving to the cloud.
- **Local-first ringing**: the synced lists land in NVS; the minute-tick
  alarm engine reads NVS + the NTP-synced clock. Wi-Fi down at 07:00 does
  not stop the 07:00 alarm.
- **Conflict safety**: a dismiss on the device (one-shot alarms switch
  themselves off) is queued as `alarm.put {enabled:false}` and pushed up,
  so the web app and the Ventuno Q see the same state.
- The device's own web UI (`deskbuddy.local`) keeps working offline: its
  REST handlers now speak ids and queue cloud ops when the cloud is on.

## The Ventuno Q side

`cloud/ventuno-q/VentunoQ_PlannerClient.ino` is a complete reference client
(WiFi + HTTPClient + ArduinoJson): poll, parse to local structs, minute
check against the local clock, dismiss-by-op. The endpoint is the same
`/api/sync` — there is no device-specific API.

## Data flow example (evening reminder)

1. You add a 21:30 reminder "Drink water" for Nov 15 in the Vercel UI.
   `POST /api/sync` `{op:"alarm.put", alarm:{...}}` → rev 41→42. (1 read + 1 write)
2. Both devices poll within 30 s; each sees `rev 42 > 41`, gets the full
   doc, swap it into NVS. (1 read each)
3. 21:30 on Nov 15: each device's local minute-tick fires the reminder from
   NVS + local clock. No network involved in the ring.
4. You tap the DeskBuddy. It disables the one-shot locally and queues
   `alarm.put {enabled:false}`; ~20 s later it pushes (1 read + 1 write).
5. The Ventuno Q's next poll (or its own local dismiss) sees the change and
   mirrors it. No double-ring, no lost state.
