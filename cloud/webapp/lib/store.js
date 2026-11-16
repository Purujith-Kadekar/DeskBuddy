// ============================================================
//  lib/store.js - the single-document planner store
//
//  ONE Firestore document holds the entire planner:
//      collection "planner", doc "main"
//      { rev: Number, tasks: [...], alarms: [...] }
//
//  Why one document:
//   - a device poll is exactly ONE read (or zero: the API answers
//     304 from the rev alone when nothing changed)
//   - a change is ONE read + ONE write inside a Firestore
//     transaction, and the write always replaces the whole
//     document: no partial updates can ever happen
//   - the revision counter gives cheap optimistic concurrency:
//     a client that sends a stale baseRev gets 409 + the fresh
//     state, and simply retries on top of it
//
//  MOCK MODE (default): if the FIREBASE_* env vars are missing the
//  same API is served from an in-memory object (plus a JSON file
//  in dev so restarts keep data). This is what "light mock
//  database" means in practice: develop without Firebase, flip
//  the env vars on Vercel and the exact same routes hit Firestore.
// ============================================================

const fs = require('fs');
const path = require('path');

// ---- wire format (identical to the buddy's own web API) ----
// alarm: { id, enabled, type: "alarm"|"reminder", kind: "once"|"daily"|"weekly"|"countdown",
//          time: "HH:MM", date: "YYYY-MM-DD", weekdays: 0..127, melody: 0..8, label }
// task : { id, title, date, "YYYY-MM-DD", end, time, done }
// op   : { op: "alarm.put"|"alarm.del"|"task.put"|"task.del", id, alarm?|task? }

const MAX_ENTRIES = 32;   // mirrors MAX_ALARMS / MAX_TASKS in the firmware
// ids: 12 lowercase chars total - 'V'+11 hex from the server, 'L'+11 hex
// from a device. The letter prefix keeps device and server ids from ever
// colliding without any coordination.
const ID_RE = /^[VL][0-9a-f]{1,11}$/;

// ---------------- validation ----------------

function validDate(y, m, d) {
  if (y < 2021 || y > 2099 || m < 1 || m > 12 || d < 1) return false;
  const dim = [31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31];
  let max = dim[m - 1];
  if (m === 2 && ((y % 4 === 0 && y % 100 !== 0) || y % 400 === 0)) max = 29;
  return d <= max;
}

function parseDate(s) {
  if (typeof s !== 'string' || s.length < 10) return null;
  const y = +s.slice(0, 4), m = +s.slice(5, 7), d = +s.slice(8, 10);
  return validDate(y, m, d) ? { y, m, d } : null;
}

function parseTime(s) {
  if (typeof s !== 'string' || s.length < 4 || s.indexOf(':') < 1) return null;
  const h = +s.slice(0, s.indexOf(':')), m = +s.slice(s.indexOf(':') + 1);
  return (h >= 0 && h <= 23 && m >= 0 && m <= 59) ? { h, m } : null;
}

function sanitizeLabel(s, cap) {
  return String(s || '')
    .split('')
    .filter(c => c.charCodeAt(0) >= 32 && c.charCodeAt(0) <= 126 && c !== '"' && c !== '\\' && c !== '<')
    .join('')
    .slice(0, cap - 1);
}

// canonical id = uppercase 'V'/'L' + lowercase hex (exactly what the firmware
// generates and accepts). Lenient on input case, strict on what is stored.
function cleanId(s) {
  if (typeof s !== 'string' || s.length < 2) return null;
  const c = s[0].toUpperCase() + s.slice(1).toLowerCase();
  return ID_RE.test(c) ? c : null;
}

// ---------------- entry cleaners ----------------

function cleanAlarm(a) {
  if (!a || typeof a !== 'object') return null;
  const out = {
    id: cleanId(a.id) || newId(),
    enabled: a.enabled === false ? false : true,
    type: a.type === 'reminder' ? 'reminder' : 'alarm',
    kind: ['once', 'daily', 'weekly', 'countdown'].includes(a.kind) ? a.kind : 'once',
    time: typeof a.time === 'string' && parseTime(a.time) ? a.time : null,
    date: '',
    weekdays: (Number.isInteger(a.weekdays) ? a.weekdays : 0) & 0x7f,
    melody: Number.isInteger(a.melody) ? Math.min(8, Math.max(0, a.melody)) : 0,
    label: sanitizeLabel(a.label, 32),
  };
  if (!out.time) return null;
  if (out.type === 'reminder') out.melody = out.melody >= 6 ? out.melody : 6;   // reminder tones
  else out.melody = out.melody <= 5 ? out.melody : 0;                            // alarm tones
  if (out.kind === 'once' || out.kind === 'countdown') {
    if (!parseDate(a.date)) return null;
    out.date = a.date.slice(0, 10);
  }
  if (out.kind === 'weekly' && out.weekdays === 0) return null;
  return out;
}

function cleanTask(t) {
  if (!t || typeof t !== 'object') return null;
  const out = {
    id: cleanId(t.id) || newId(),
    title: sanitizeLabel(t.title, 24),
    date: '',
    end: '',
    time: '',
    done: t.done === true,
  };
  if (!out.title) return null;
  const d = parseDate(t.date);
  if (!d) return null;
  out.date = t.date.slice(0, 10);
  if (typeof t.end === 'string' && t.end.length >= 10) {
    const e = parseDate(t.end);
    if (!e || (e.y * 10000 + e.m * 100 + e.d) < (d.y * 10000 + d.m * 100 + d.d)) return null;
    out.end = t.end.slice(0, 10);
  }
  if (typeof t.time === 'string' && parseTime(t.time)) out.time = t.time.slice(0, 5);
  return out;
}

// server ids start with "V", device ids with "L" - no collisions
let idCounter = 0;
function newId() {
  const t = Date.now().toString(16).slice(-5);
  const r = Math.floor(Math.random() * 0xfffff).toString(16).padStart(5, '0');
  const c = (idCounter++).toString(16).padStart(2, '0');
  return ('V' + `${t}${r}${c}`.toLowerCase()).slice(0, 12);
}

// ---------------- op application (pure) ----------------

// ---- making room: when a list is full, a NEW entry may evict a stale one ----
// (the firmware stores at most 32 of each, so the cloud doc keeps the same cap)
// Safe to drop: finished tasks and tasks that ended > 1 day ago; switched-off
// one-shot alarms/reminders. Oldest first. "today" is UTC minus a day of slack,
// so a device in any timezone never loses something still current.
function staleCutoffKey() {
  const d = new Date(Date.now() - 36 * 3600 * 1000);
  return d.getUTCFullYear() * 10000 + (d.getUTCMonth() + 1) * 100 + d.getUTCDate();
}
const dkeyOf = (s) => +s.slice(0, 4) * 10000 + +s.slice(5, 7) * 100 + +s.slice(8, 10);

function evictStaleTask(state) {
  const cut = staleCutoffKey();
  let best = -1, bestKey = Infinity;
  state.tasks.forEach((t, i) => {
    const k = dkeyOf(t.end || t.date);
    if ((t.done || k < cut) && k < bestKey) { best = i; bestKey = k; }
  });
  if (best < 0) return false;
  state.tasks.splice(best, 1);
  return true;
}

function evictStaleAlarm(state) {
  const cut = staleCutoffKey();
  let best = -1, bestKey = Infinity;
  state.alarms.forEach((a, i) => {
    if ((a.kind !== 'once' && a.kind !== 'countdown') || !a.date) return;
    const k = dkeyOf(a.date);
    if ((!a.enabled || k < cut) && k < bestKey) { best = i; bestKey = k; }
  });
  if (best < 0) return false;
  state.alarms.splice(best, 1);
  return true;
}

// Applies one op to a {tasks, alarms} state IN PLACE.
// Returns 'changed' | 'unchanged' (valid, identical content) | 'rejected'
// (invalid entry, unknown id on delete is 'unchanged', list full of live entries).
function applyOpEx(state, op) {
  if (!op || typeof op !== 'object') return 'rejected';
  const id = cleanId(op.id);
  const kind = op.op;
  if (kind === 'alarm.put') {
    const a = cleanAlarm(op.alarm || op.entry);
    if (!a) return 'rejected';
    if (id) a.id = id;                       // the op id wins (the device knows its entry)
    const i = state.alarms.findIndex(x => x.id === a.id);
    if (i >= 0) {
      if (JSON.stringify(state.alarms[i]) === JSON.stringify(a)) return 'unchanged';
      state.alarms[i] = a;
      return 'changed';
    }
    if (state.alarms.length >= MAX_ENTRIES && !evictStaleAlarm(state)) return 'rejected';
    state.alarms.push(a);
    return 'changed';
  }
  if (kind === 'task.put') {
    const t = cleanTask(op.task || op.entry);
    if (!t) return 'rejected';
    if (id) t.id = id;
    const i = state.tasks.findIndex(x => x.id === t.id);
    if (i >= 0) {
      if (JSON.stringify(state.tasks[i]) === JSON.stringify(t)) return 'unchanged';
      state.tasks[i] = t;
      return 'changed';
    }
    if (state.tasks.length >= MAX_ENTRIES && !evictStaleTask(state)) return 'rejected';
    state.tasks.push(t);
    return 'changed';
  }
  if (kind === 'alarm.del' || kind === 'task.del') {
    if (!id) return 'rejected';
    const key = kind === 'alarm.del' ? 'alarms' : 'tasks';
    const before = state[key].length;
    state[key] = state[key].filter(x => x.id !== id);
    return state[key].length !== before ? 'changed' : 'unchanged';
  }
  return 'rejected';
}

function applyOp(state, op) { return applyOpEx(state, op) === 'changed'; }

// -> { changed: bool, rejected: number }
function applyOpsEx(state, ops) {
  const out = { changed: false, rejected: 0 };
  if (!Array.isArray(ops)) return out;
  for (const op of ops) {
    const r = applyOpEx(state, op);
    if (r === 'changed') out.changed = true;
    else if (r === 'rejected') out.rejected++;
  }
  return out;
}

function applyOps(state, ops) { return applyOpsEx(state, ops).changed; }

// ---------------- mock store (default, no env vars) ----------------

const MOCK_FILE = process.env.MOCK_STORE_FILE ||
  path.join('/tmp', 'planner-mock-store.json');

let mockDoc = null;

function loadMock() {
  if (mockDoc) return mockDoc;
  try {
    mockDoc = JSON.parse(fs.readFileSync(MOCK_FILE, 'utf8'));
  } catch {
    mockDoc = { rev: 0, tasks: [], alarms: [] };
  }
  return mockDoc;
}

function saveMock(doc) {
  try { fs.writeFileSync(MOCK_FILE, JSON.stringify(doc)); } catch { /* dev-only persistence */ }
}

// ---------------- Firestore store (env vars set) ----------------

let firestore = null;
function getFirestoreDb() {
  if (firestore) return firestore;
  const admin = require('firebase-admin');
  if (!process.env.FIREBASE_SERVICE_ACCOUNT) {
    throw new Error('FIREBASE_SERVICE_ACCOUNT env var missing');
  }
  const cred = JSON.parse(process.env.FIREBASE_SERVICE_ACCOUNT);
  // pasted env vars sometimes carry the key's newlines as literal backslash-n
  if (cred.private_key) cred.private_key = cred.private_key.replace(/\\n/g, '\n');
  const app = admin.apps.length ? admin.app() : admin.initializeApp({ credential: admin.credential.cert(cred) });
  firestore = app.firestore();
  return firestore;
}

// Is Firestore mode active? (env var style 1: service account JSON)
function firestoreActive() {
  if (process.env.FIREBASE_SERVICE_ACCOUNT) return true;
  // On Vercel the /tmp mock store is per-instance and wiped on cold starts:
  // silently falling back to it would LOSE the user's calendar. Fail loudly.
  if (process.env.VERCEL && process.env.ALLOW_MOCK_ON_VERCEL !== '1') {
    throw new Error('FIREBASE_SERVICE_ACCOUNT is not set in Vercel env vars - refusing to use the temporary mock store');
  }
  return false;
}

// a stored doc is trusted only after shape-normalising (never crash on a hand-edited doc)
function normDoc(d) {
  return {
    rev: Number.isInteger(d && d.rev) ? d.rev : 0,
    tasks: Array.isArray(d && d.tasks) ? d.tasks : [],
    alarms: Array.isArray(d && d.alarms) ? d.alarms : [],
  };
}

// ---------------- the store API ----------------
// getDoc(rev)  -> { rev, tasks, alarms } | null (means: 304, client is current)
// applyOpsTx(baseRev, ops) -> { status: 200|409, doc }

async function getDoc(clientRev) {
  if (firestoreActive()) {
    const db = getFirestoreDb();
    const snap = await db.collection('planner').doc('main').get();
    if (!snap.exists) {
      const doc = { rev: 0, tasks: [], alarms: [] };
      // create on first touch (1 write)
      await db.collection('planner').doc('main').set(doc);
      return doc;
    }
    const doc = normDoc(snap.data());
    // rev 0 + empty lists = the doc was never written: always send it, so a
    // fresh device (booting at rev=0) sees the empty state and can seed it
    if (typeof clientRev === 'number' && clientRev === doc.rev && doc.rev > 0) return null;  // 304
    return doc;
  }
  const doc = loadMock();
  // a never-written doc (rev 0, empty) must always answer with the full
  // state: a fresh device boots with rev=0 and needs to see the empty doc
  if (typeof clientRev === 'number' && clientRev === doc.rev && doc.rev > 0) return null;
  return doc;
}

async function applyOpsTx(baseRev, ops) {
  if (firestoreActive()) {
    const db = getFirestoreDb();
    const ref = db.collection('planner').doc('main');
    // ONE transaction = ONE read + (at most) ONE write, whole document.
    // A partial update is impossible by construction.
    return db.runTransaction(async (tx) => {
      const snap = await tx.get(ref);
      const doc = snap.exists ? normDoc(snap.data()) : { rev: 0, tasks: [], alarms: [] };
      if (typeof baseRev === 'number' && baseRev !== doc.rev) {
        return { status: 409, doc };          // stale: caller sends the fresh state back
      }
      const next = { rev: doc.rev + 1, tasks: [...doc.tasks], alarms: [...doc.alarms] };
      const { changed, rejected } = applyOpsEx(next, ops);
      if (changed) {
        tx.set(ref, next);                    // whole-document write, exactly once
        return { status: 200, doc: next, rejected };
      }
      return { status: 200, doc, rejected };  // no-op: nothing written, no rev bump
    });
  }
  // mock mode: same semantics, synchronous
  const doc = loadMock();
  if (typeof baseRev === 'number' && baseRev !== doc.rev) {
    return { status: 409, doc };
  }
  const next = { rev: doc.rev + 1, tasks: [...doc.tasks], alarms: [...doc.alarms] };
  const { changed, rejected } = applyOpsEx(next, ops);
  if (changed) {
    mockDoc = next;
    saveMock(next);
    return { status: 200, doc: next, rejected };
  }
  return { status: 200, doc, rejected };
}

module.exports = {
  getDoc,
  applyOpsTx,
  applyOps,
  applyOpsEx,      // exposed for the UI's optimistic preview (same rules client-side)
  cleanAlarm,
  cleanTask,
  newId,
  firestoreActive,
  MAX_ENTRIES,
};
