// ============================================================
//  /api/sync - the ONE endpoint devices and the UI share
//
//  GET  /api/sync?dev=<name>&rev=<n>
//       -> 200 { ok, rev, tasks, alarms }   full state (1 Firestore read)
//       -> 304                               client already current (0 reads)
//
//  POST /api/sync   body: { dev, baseRev, ops: [...] }
//       -> 200 { ok, rev, tasks, alarms }   ops applied (1 read + 1 write)
//       -> 409 { ok: false, rev, tasks, alarms }  stale baseRev: the client
//              re-applies its ops on the returned fresh state and retries
//
//  Ops (id is the stable 12-hex entry id):
//    { op: "alarm.put", id, alarm: { ... } }
//    { op: "alarm.del", id }
//    { op: "task.put",  id, task:  { ... } }
//    { op: "task.del",  id }
//
//  Auth: if SYNC_API_KEY is set, requests must carry it in the
//  X-Api-Key header (the firmware sends the same CLOUD_API_KEY).
// ============================================================

import { getDoc, applyOpsTx } from '../../../lib/store';

// never cache: every poll must reach the store (the 304 shortcut is done by hand)
export const dynamic = 'force-dynamic';
export const runtime = 'nodejs';

function authorized(req) {
  const key = process.env.SYNC_API_KEY;
  if (!key) return true;                     // no key configured: open (dev)
  const sent = req.headers.get('x-api-key') || '';   // Web Request: Headers.get(), not [] indexing
  return sent === key;
}

function stateBody(doc, extra) {
  return {
    ok: true,
    rev: doc.rev,
    tasks: doc.tasks,
    alarms: doc.alarms,
    ...(extra || {}),
  };
}

export async function GET(req) {
  if (!authorized(req)) {
    return Response.json({ ok: false, error: 'unauthorized' }, { status: 401 });
  }
  const url = new URL(req.url);
  const clientRev = parseInt(url.searchParams.get('rev') || '-1', 10);
  try {
    const doc = await getDoc(Number.isNaN(clientRev) ? undefined : clientRev);
    if (doc === null) {
      // nothing changed since the caller's rev: 304 keeps the poll at 0 reads
      return new Response(null, { status: 304 });
    }
    return Response.json(stateBody(doc), {
      headers: { 'Cache-Control': 'no-store' },
    });
  } catch (e) {
    return Response.json({ ok: false, error: String(e && e.message || e) }, { status: 500 });
  }
}

export async function POST(req) {
  if (!authorized(req)) {
    return Response.json({ ok: false, error: 'unauthorized' }, { status: 401 });
  }
  let body;
  try {
    body = await req.json();
  } catch {
    return Response.json({ ok: false, error: 'invalid JSON' }, { status: 400 });
  }
  const dev = typeof body.dev === 'string' ? body.dev.slice(0, 24) : 'unknown';
  const baseRev = typeof body.baseRev === 'number' ? body.baseRev : undefined;
  if (!Array.isArray(body.ops)) {
    return Response.json({ ok: false, error: 'ops must be an array' }, { status: 400 });
  }
  if (body.ops.length > 64) {
    return Response.json({ ok: false, error: 'too many ops (max 64)' }, { status: 400 });
  }
  try {
    const { status, doc, rejected } = await applyOpsTx(baseRev, body.ops);
    if (status === 409) {
      // optimistic-concurrency conflict: return the fresh state so the client
      // can rebase and retry. Cost so far: exactly 1 read, 0 writes.
      return Response.json(
        { ok: false, conflict: true, error: 'stale baseRev', ...stateBody(doc) },
        { status: 409, headers: { 'Cache-Control': 'no-store' } }
      );
    }
    console.log(`[sync] ${dev} applied ${body.ops.length} op(s) -> rev ${doc.rev}`);
    // rejected > 0 means some ops were refused (invalid entry, or the 32-entry limit)
    return Response.json(stateBody(doc, { rejected: rejected || 0 }), {
      headers: { 'Cache-Control': 'no-store' },
    });
  } catch (e) {
    return Response.json({ ok: false, error: String(e && e.message || e) }, { status: 500 });
  }
}
