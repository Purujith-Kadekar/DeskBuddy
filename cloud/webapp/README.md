# Planner Webapp (Vercel + Firestore)

Cloud-first planner — tasks, reminders, alarms — shared by the **DeskBuddy**
(ESP32-C3 desk companion) and the **Ventuno Q**. This app is the single
source of truth; both devices sync a local copy from it and ring alarms
entirely locally.

Read [`ARCHITECTURE.md`](./ARCHITECTURE.md) for the full protocol.

## Quick start (mock store, no Firebase needed)

```bash
npm install
npm run dev          # http://localhost:3000
```

The app runs on a built-in mock store (in-memory + a JSON file under /tmp),
with exactly the same API and semantics as the Firestore mode. Develop and
test everything locally, then flip the env vars.

## Test the sync protocol

With a server running (`npm run dev` or `npm run build && npm start`):

```bash
node test/sync-protocol.js http://localhost:3000
```

assertions covering: fresh GET, 304 poll, device seed, multi-device
edits, 409 + rebase-retry, invalid-op rejection, idempotent re-puts.

## Deploy to Vercel

**Option A - CLI, no GitHub (direct upload):** unzip, then in the folder run `npx vercel --prod`.

**Option B - GitHub:**

1. Push this folder to a GitHub repo.
2. In Vercel: **New Project** → import the repo (Next.js is auto-detected;
   `vercel.json` only pins the framework). Node 22.x (Project Settings → General).
3. Set environment variables (Project → Settings → Environment Variables):
   - `SYNC_API_KEY` — a long random string; put the same value in the
     DeskBuddy's `include/wifi_credentials.h` as `CLOUD_API_KEY` and in the
     Ventuno Q sketch as `SYNC_KEY`.
   - `FIREBASE_SERVICE_ACCOUNT` (**required**: on Vercel the app refuses to start on the temporary mock store) — the **full JSON of a Firebase service
     account key** pasted as one line. Firestore mode activates when this
     is present (without it, the mock store is used — fine for a demo, not
     for production).
4. Deploy. The API is at `https://<your-app>.vercel.app/api/sync`, the UI
   at the root.

### Firebase setup

1. Create a Firebase project → Firestore Database (production mode).
2. Project settings → Service accounts → **Generate new private key**.
3. That JSON (as one line) is `FIREBASE_SERVICE_ACCOUNT`.
4. The app touches exactly one document: `planner/main`. Server-only access
   means the default locked-down rules work as-is:

```
rules_version = '2';
service cloud.firestore {
  match /databases/{database}/documents {
    match /{document=**} {
      allow read, write: if false;    // the Admin SDK bypasses rules
    }
  }
}
```

## Structure

```
app/
  page.js            planner UI (calendar + list + editors)
  layout.js          root layout
  globals.css        styling
  api/sync/route.js  THE endpoint (GET poll / POST ops)
lib/
  store.js           mock + Firestore single-document store, validation, ops
test/
  sync-protocol.js   protocol test suite
(the Ventuno Q client lives in ../ventuno-q, the DeskBuddy firmware in ../firmware)
ARCHITECTURE.md      the protocol + cost model
```

## Firestore cost model

| action | reads | writes |
| --- | --- | --- |
| device poll, nothing new (HTTP 304) | 1 (rev check; saves bandwidth only) | 0 |
| device poll, change available | 1 | 0 |
| any edit (1 op or 12 ops) | 1 | 1 (whole doc, transactional) |
| conflicting edit (409 + retry) | 2 | 1 |

Two devices polling every 30 s ≈ 4 reads/min (~5,800/day; web tab open adds ~5,800/day) - far inside the
50,000/day free tier; each actual change costs exactly 2 quota units no matter how many entries it touches. Updates
are always whole-document — partial writes cannot happen.
