# Ventuno Q client

`VentunoQ_PlannerClient.ino` is a reference client for the shared planner: it polls
`GET /api/sync?dev=ventunoq&rev=N`, keeps a local copy, checks alarms against the local clock and
pushes edits with `POST /api/sync` (`baseRev` + `ops`, one retry on a 409 conflict).

Set `WIFI_SSID`, `WIFI_PASS`, `SYNC_BASE` (your Vercel URL, no trailing slash) and `SYNC_KEY`
(same as `SYNC_API_KEY`) at the top of the sketch. The full protocol is in
`../webapp/ARCHITECTURE.md`.
