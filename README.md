# DeskBuddy

An ESP32-C3 desk companion: OLED face, touch, tilt, buzzer, alarms, reminders, tasks, weather,
study timer. This repository holds **two editions** of the firmware and the cloud planner that
goes with the second one.

| | `standalone/` | `cloud/` |
| --- | --- | --- |
| Firmware version | v3.17 | v3.21 |
| Needs internet / accounts | No (WiFi only, for its own web app) | Yes: Vercel + Firebase |
| Planner (alarms, reminders, tasks) | Stored on the buddy (NVS), edited in its own web app | Stored in Firestore, shared by the web app, the buddy and the Ventuno Q |
| Rings with WiFi down | Yes | Yes (local copy + NTP clock) |
| Ventuno Q can drive it | No | Yes: USB serial commands and the shared planner API |
| Extras | | Face parts (eyes / mouth / hands), 32 variants per expression, `[TOUCH]` events to the host |

Pick one edition per buddy. They share the same wiring and hardware.

```
DeskBuddy/
  standalone/          offline firmware, v3.17 (PlatformIO project)
  cloud/
    firmware/          cloud-synced firmware, v3.21 (PlatformIO project)
    webapp/            Next.js planner for Vercel (Firestore database)
    ventuno-q/         reference client sketch for the Ventuno Q
```

## Standalone
Open `standalone/` in PlatformIO. Wiring, WiFi setup and OTA updates are all in
[`standalone/README.md`](standalone/README.md).

## Cloud
Read [`cloud/README.md`](cloud/README.md): deploy the webapp, point the firmware at it, connect the
Ventuno Q.

## Secrets
`include/wifi_credentials.h` (WiFi + OTA password + cloud key) and every `.env` file are
git-ignored. Only `wifi_credentials.example.h` and `.env.example` are committed.
