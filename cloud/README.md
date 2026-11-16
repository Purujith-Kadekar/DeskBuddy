# DeskBuddy Cloud edition

Three parts, one planner. Vercel + Firestore is the single source of truth; the buddy and the
Ventuno Q each keep a local copy and ring alarms from it, so nothing depends on WiFi at ring time.

```
 Web app (Vercel)  <--->  /api/sync  <--->  Firestore  planner/main  (1 document)
                              ^
              poll 30 s / push ops
                 |                 |
            DeskBuddy         Ventuno Q
            (firmware/)       (ventuno-q/)
```

## 1. Firebase
1. Firebase console: create a project, then Firestore Database (production mode).
2. Rules: deny everything (`allow read, write: if false;`). The server uses the Admin SDK, which
   bypasses rules.
3. Project settings, Service accounts, **Generate new private key**. The JSON, pasted as one line,
   is `FIREBASE_SERVICE_ACCOUNT`.

## 2. Vercel (webapp)
In the Vercel project set **Root Directory** to `cloud/webapp` (or run `npx vercel --prod` from
inside that folder). Node 22.x. Environment variables:

| Name | Value |
| --- | --- |
| `FIREBASE_SERVICE_ACCOUNT` | the full service-account JSON, one line (required) |
| `SYNC_API_KEY` | a long random string; the devices send it as `X-Api-Key` |

Details, protocol and the cost model: [`webapp/README.md`](webapp/README.md) and
[`webapp/ARCHITECTURE.md`](webapp/ARCHITECTURE.md). Protocol tests: `node test/sync-protocol.js
http://localhost:3000` from `webapp/` with the server running.

## 3. DeskBuddy firmware
1. Copy `firmware/include/wifi_credentials.example.h` to `firmware/include/wifi_credentials.h`
   and fill in your WiFi, `OTA_PASSWORD`, and uncomment
   `CLOUD_BASE_URL` (your `https://<app>.vercel.app`) and `CLOUD_API_KEY` (same value as
   `SYNC_API_KEY`).
2. Build and flash with PlatformIO (see `firmware/README.md`).
3. On first contact with an empty cloud document the buddy uploads its local planner, so nothing
   is lost.

## 4. Ventuno Q
- **Over WiFi:** `ventuno-q/VentunoQ_PlannerClient.ino` speaks the same `/api/sync` protocol. Set
  `SYNC_BASE` and `SYNC_KEY` in it. It is a reference client written for Arduino-style
  `WiFi.h`/`HTTPClient`; on a Linux-side program, the same two HTTP calls work from Python or curl.
- **Over USB:** the firmware reads commands from the USB serial port (`ALARM`, `TASK`, `DEL`,
  `FACE`, ...). Edits made that way are queued to the cloud as well.
