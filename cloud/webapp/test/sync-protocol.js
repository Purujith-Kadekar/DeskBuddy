#!/usr/bin/env node
// ============================================================
//  test/sync-protocol.js - exercises /api/sync exactly the way
//  the firmware does. Run with the dev/production server up:
//    node test/sync-protocol.js http://localhost:3000
// ============================================================
const BASE = process.argv[2] || 'http://localhost:3000';
let passed = 0, failed = 0;

function ok(cond, name, extra) {
  if (cond) { passed++; console.log(`  PASS  ${name}`); }
  else { failed++; console.log(`  FAIL  ${name}${extra ? ' — ' + extra : ''}`); }
}

async function get(path, headers) {
  const r = await fetch(BASE + path, { headers });
  const text = await r.text();
  let j = null;
  try { j = JSON.parse(text); } catch {}
  return { status: r.status, json: j, text };
}

async function post(path, body, headers) {
  const r = await fetch(BASE + path, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json', ...(headers || {}) },
    body: JSON.stringify(body),
  });
  const text = await r.text();
  let j = null;
  try { j = JSON.parse(text); } catch {}
  return { status: r.status, json: j, text };
}

(async () => {
  console.log(`Testing sync protocol against ${BASE}\n`);

  // ---- 1. empty-state GET (what a fresh device does) ----
  let r = await get('/api/sync?dev=deskbuddy&rev=0');
  ok(r.status === 200 && r.json && r.json.ok, 'fresh GET returns 200 + state');
  ok(Array.isArray(r.json.alarms) && Array.isArray(r.json.tasks), 'state has alarms[] and tasks[]');
  const rev0 = r.json.rev;
  ok(typeof rev0 === 'number', 'state carries a rev number');

  // ---- 2. 304 path: an empty, never-written doc always answers 200 (a
  //      fresh device booting at rev=0 must see the empty state to seed it),
  //      so the 304 is checked after the seed POST below ----

  // ---- 3. seed POST (device uploads its planner, like cloudSeedIfNeeded) ----
  r = await post('/api/sync', {
    dev: 'deskbuddy',
    baseRev: rev0,
    ops: [
      { op: 'alarm.put', id: 'Labc123def45', alarm: { id: 'Labc123def45', enabled: true, type: 'alarm', kind: 'daily', time: '07:00', date: '', weekdays: 0, melody: 0, label: 'Wake up' } },
      { op: 'alarm.put', id: 'Lfed654cba32', alarm: { id: 'Lfed654cba32', enabled: true, type: 'reminder', kind: 'once', time: '21:30', date: '2026-11-15', weekdays: 0, melody: 7, label: 'Drink water' } },
      { op: 'task.put', id: 'L11122233344', task: { id: 'L11122233344', title: 'Ship the firmware', date: '2026-11-10', end: '2026-11-12', time: '10:00', done: false } },
    ],
  });
  ok(r.status === 200 && r.json.ok, 'seed POST returns 200');
  const rev1 = r.json.rev;
  ok(rev1 === rev0 + 1, `rev bumped by exactly 1 (${rev0} -> ${rev1})`);
  ok(r.json.alarms.length === 2 && r.json.tasks.length === 1, 'echo state contains the seeded entries');

  // ---- 3b. NOW the 304 path: current rev, non-empty doc ----
  r = await get(`/api/sync?dev=deskbuddy&rev=${rev1}`);
  ok(r.status === 304, `poll with current rev after a write -> 304 (got ${r.status})`);

  // ---- 4. device polls with old rev: gets the new state ----
  r = await get(`/api/sync?dev=deskbuddy&rev=${rev0}`);
  ok(r.status === 200 && r.json.alarms.length === 2, 'poll with stale rev -> full state');

  // ---- 5. Ventuno Q edits on top of rev1 (the other device) ----
  r = await post('/api/sync', {
    dev: 'ventunoq',
    baseRev: rev1,
    ops: [{ op: 'task.put', id: 'v777888999000', task: { id: 'v777888999000', title: 'Charge Ventuno Q', date: '2026-11-11', end: '', time: '', done: false } }],
  });
  ok(r.status === 200, 'second device POST on rev1 -> 200');
  const rev2 = r.json.rev;
  ok(rev2 === rev1 + 1, `rev bumped again (${rev1} -> ${rev2})`);
  ok(r.json.tasks.length === 2, 'both tasks present after the second device writes');

  // ---- 6. 409: deskbuddy still on rev1 and writes ----
  r = await post('/api/sync', {
    dev: 'deskbuddy',
    baseRev: rev1,
    ops: [{ op: 'alarm.del', id: 'Labc123def45' }],
  });
  ok(r.status === 409 && r.json.conflict === true, 'stale baseRev -> 409 + conflict flag');
  ok(r.json.rev === rev2, '409 body carries the fresh state (rev matches)');
  ok(Array.isArray(r.json.alarms), '409 body has alarms[] for rebasing');

  // ---- 7. deskbuddy rebases and retries (what the firmware does) ----
  r = await post('/api/sync', {
    dev: 'deskbuddy',
    baseRev: rev2,
    ops: [{ op: 'alarm.del', id: 'Labc123def45' }],
  });
  ok(r.status === 200 && r.json.alarms.length === 1, 'rebased retry succeeds');

  // ---- 8. validation: garbage is rejected, state untouched ----
  const before = await get('/api/sync?dev=x&rev=-1');
  r = await post('/api/sync', {
    dev: 'web',
    baseRev: before.json.rev,
    ops: [
      { op: 'alarm.put', id: 'bad', alarm: { kind: 'once', time: '99:99', date: '2026-13-45' } },      // invalid everything
      { op: 'task.put', id: 'bad2', task: { title: '', date: '2026-02-30' } },                          // Feb 30
      { op: 'task.put', id: 'bad3', task: { title: 'End before start', date: '2026-11-10', end: '2026-11-01' } },
    ],
  });
  ok(r.status === 200 && r.json.rev === before.json.rev, 'invalid ops change nothing (rev not bumped)');
  ok(r.json.tasks.length === before.json.tasks.length, 'no entries added by invalid ops');

  // ---- 9. done-toggle + idempotent put ----
  const task = before.json.tasks[0];
  r = await post('/api/sync', {
    dev: 'web',
    baseRev: before.json.rev,
    ops: [{ op: 'task.put', id: task.id, task: { ...task, done: true } }],
  });
  ok(r.status === 200 && r.json.tasks.find((t) => t.id === task.id).done === true, 'task checked off via task.put');

  const revAfterToggle = r.json.rev;
  r = await post('/api/sync', {
    dev: 'web',
    baseRev: revAfterToggle,
    ops: [{ op: 'task.put', id: task.id, task: { ...task, done: true } }],  // identical content again
  });
  ok(r.json.rev === revAfterToggle, 'identical re-put is a no-op (no extra write/rev)');

  // ---- 10. device id case survives the round trip (firmware ids are 'L' + hex) ----
  r = await get('/api/sync?dev=deskbuddy&rev=-1');
  ok(r.json.alarms.some((a) => a.id === 'Lfed654cba32') && r.json.tasks.some((t) => t.id === 'L11122233344'),
     "device ids ('L...') are stored exactly as sent");
  ok([...r.json.alarms, ...r.json.tasks].every((e) => /^[VL][0-9a-f]{1,11}$/.test(e.id)), 'every stored id is V/L + lowercase hex');

  // ---- 11. full list: stale entries make room, live ones are refused loudly ----
  let cur = r.json.rev;
  const ops = [];
  for (let i = 0; i < 40; i++) {
    const day = String((i % 27) + 1).padStart(2, '0');
    ops.push({ op: 'task.put', id: 'L' + String(i).padStart(11, '0'), task: { title: 'fill ' + i, date: '2099-12-' + day, done: false } });
  }
  r = await post('/api/sync', { dev: 'web', baseRev: cur, ops });
  ok(r.status === 200 && r.json.tasks.length === 32, 'task list is capped at 32');
  ok(r.json.rejected > 0, 'ops beyond the cap are reported in "rejected" (UI can warn)');

  // ---- 12. API key (only when the server was started with SYNC_API_KEY=<arg 3>) ----
  const key = process.argv[3];
  if (key) {
    r = await get('/api/sync?dev=t&rev=0');
    ok(r.status === 401, 'no key -> 401');
    r = await get('/api/sync?dev=t&rev=0', { 'X-Api-Key': 'wrong' });
    ok(r.status === 401, 'wrong key -> 401');
    r = await get('/api/sync?dev=t&rev=-1', { 'X-Api-Key': key });
    ok(r.status === 200 && r.json.ok, 'correct key -> 200');
  }

  console.log(`\n${passed} passed, ${failed} failed`);
  process.exit(failed ? 1 : 0);
})().catch((e) => { console.error(e); process.exit(1); });
