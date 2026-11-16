'use client';

// ============================================================
//  Planner page (ported from the buddy's Planner tab design)
//  - month calendar with task / alarm dots
//  - one-day view: tick tasks, edit everything
//  - list view with filters (All / Alarms / Reminders / Tasks)
//  - modal editors for alarms, reminders and tasks
//
//  Every edit goes through POST /api/sync as ONE op with the
//  client's current rev. The server applies it atomically and
//  returns the whole new state - the UI just renders the reply.
//  On 409 (someone else wrote first) it retries once on the
//  fresh state: total cost stays 1 read + 1 write per edit.
// ============================================================

import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

// ---- constants (kept in lockstep with the firmware) ----
const MELS = ['Classic Alarm', 'Gentle Chime', 'Robot Beeps', 'Sunrise Arp', 'Birdsong', 'Radar Ping', 'Tri-tone', 'Water Drop', 'Double Pulse'];
const ALARM_TONES = [0, 1, 2, 3, 4, 5];
const REMINDER_TONES = [6, 7, 8];
const DAYS = ['Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat', 'Sun'];
const DOW = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
const MON = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const MONFULL = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];

// ---- tiny date helpers (all local time, like the device) ----
const pad2 = (n) => String(n).padStart(2, '0');
const isoOf = (d) => `${d.getFullYear()}-${pad2(d.getMonth() + 1)}-${pad2(d.getDate())}`;
const dkey = (s) => +s.slice(0, 4) * 10000 + +s.slice(5, 7) * 100 + +s.slice(8, 10);
const todayISO = () => isoOf(new Date());
const dowOf = (iso) => new Date(iso + 'T12:00:00').getDay();      // 0 = Sunday
const taskOnDay = (t, k) => k >= dkey(t.date) && k <= dkey(t.end || t.date);
function alarmFiresOn(a, k) {
  if (!a.enabled) return false;
  if (a.kind === 'daily') return true;
  if (a.kind === 'weekly') {
    const js = dowOf(`${Math.floor(k / 10000)}-${pad2(Math.floor(k / 100) % 100)}-${pad2(k % 100)}`);
    return !!(a.weekdays & (1 << (js === 0 ? 6 : js - 1)));
  }
  return a.date && dkey(a.date) === k;
}
const shortDate = (s) => `${+s.slice(8, 10)} ${MON[+s.slice(5, 7) - 1]}`;
const fmtTime = (t) => {
  const h = +t.slice(0, 2);
  return `${h % 12 === 0 ? 12 : h % 12}:${t.slice(3, 5)} ${h >= 12 ? 'PM' : 'AM'}`;
};

export default function Planner() {
  const [state, setState] = useState(null);      // { rev, tasks, alarms }
  const [view, setView] = useState('cal');      // 'cal' | 'list'
  const [filter, setFilter] = useState('all');
  const [calY, setCalY] = useState(new Date().getFullYear());
  const [calM, setCalM] = useState(new Date().getMonth() + 1);
  const [selDate, setSelDate] = useState(todayISO());
  const [modal, setModal] = useState(null);     // { type:'alarm'|'task', entry }
  const [toast, setToast] = useState(null);
  const [busy, setBusy] = useState(false);
  const [syncing, setSyncing] = useState(false);
  const toastTimer = useRef(null);
  const stateRef = useRef(null);
  stateRef.current = state;

  const say = useCallback((msg, err) => {
    setToast({ msg, err });
    clearTimeout(toastTimer.current);
    toastTimer.current = setTimeout(() => setToast(null), 2400);
  }, []);

  // ---- data layer ----
  const refresh = useCallback(async () => {
    setSyncing(true);
    try {
      const r = await fetch('/api/sync?dev=web&rev=-1', { cache: 'no-store' });
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      const j = await r.json();
      setState(j);
    } catch (e) {
      say(`Could not reach the planner: ${e.message}`, true);
    } finally {
      setSyncing(false);
    }
  }, [say]);

  useEffect(() => { refresh(); }, [refresh]);

  // light background poll: catches device-side changes (dismiss etc.)
  useEffect(() => {
    const t = setInterval(async () => {
      try {
        const cur = stateRef.current;
        const r = await fetch(`/api/sync?dev=web&rev=${cur ? cur.rev : -1}`, { cache: 'no-store' });
        if (r.status === 304) return;
        if (!r.ok) return;
        const j = await r.json();
        setState(j);
      } catch { /* transient: the next tick retries */ }
    }, 15000);
    return () => clearInterval(t);
  }, []);

  // one op up; 409 -> one rebase + retry
  const pushOps = useCallback(async (ops, okMsg) => {
    setBusy(true);
    try {
      for (let attempt = 0; attempt < 2; attempt++) {
        const cur = stateRef.current;
        const r = await fetch('/api/sync', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ dev: 'web', baseRev: cur ? cur.rev : 0, ops }),
        });
        const j = await r.json().catch(() => ({}));
        if (r.status === 409) {
          setState({ rev: j.rev, tasks: j.tasks, alarms: j.alarms });  // rebase
          continue;                                                     // retry once
        }
        if (!r.ok || !j.ok) throw new Error(j.error || `HTTP ${r.status}`);
        setState({ rev: j.rev, tasks: j.tasks, alarms: j.alarms });
        if (j.rejected > 0) {
          // the server refused the entry (invalid, or 32 live entries already stored)
          throw new Error('Not saved: the planner is full (32 max) or the entry is invalid');
        }
        if (okMsg) say(okMsg);
        return true;
      }
      throw new Error('conflict: someone else changed the planner, try again');
    } catch (e) {
      say(e.message, true);
      return false;
    } finally {
      setBusy(false);
    }
  }, [say]);

  // ---- derived ----
  const alarms = state?.alarms || [];
  const tasks = state?.tasks || [];
  const counts = useMemo(() => ({
    a: alarms.filter((x) => x.type === 'alarm').length,
    r: alarms.length - alarms.filter((x) => x.type === 'alarm').length,
    t: tasks.length,
    on: alarms.filter((x) => x.enabled).length,
  }), [alarms, tasks]);

  const dayTasks = useMemo(
    () => tasks.filter((t) => taskOnDay(t, dkey(selDate))).sort((x, y) => score(x) - score(y)),
    [tasks, selDate]
  );
  const dayAlarms = useMemo(
    () => alarms.filter((a) => alarmFiresOn(a, dkey(selDate))).sort((x, y) => (x.time < y.time ? -1 : 1)),
    [alarms, selDate]
  );

  // month grid cells (Monday-first, 42 cells)
  const cells = useMemo(() => {
    const startIdx = (new Date(calY, calM - 1, 1).getDay() + 6) % 7;
    const out = [];
    const off = new Date(calY, calM - 1, 1 - startIdx);
    for (let i = 0; i < 42; i++) {
      const d = new Date(off.getFullYear(), off.getMonth(), off.getDate() + i);
      const iso = isoOf(d);
      const k = dkey(iso);
      out.push({
        iso, k, d: d.getDate(),
        inMonth: d.getMonth() + 1 === calM,
        isToday: iso === todayISO(),
        tasks: tasks.filter((t) => taskOnDay(t, k)),
        alarms: alarms.filter((a) => alarmFiresOn(a, k)),
      });
    }
    return out;
  }, [calY, calM, tasks, alarms]);

  // ---- editors ----
  const saveAlarm = (entry) => pushOps([{ op: 'alarm.put', id: entry.id, alarm: entry }],
    entry.type === 'reminder' ? 'Reminder saved' : 'Alarm saved');
  const saveTask = (entry) => pushOps([{ op: 'task.put', id: entry.id, task: entry }], 'Task saved');
  const delEntry = (kind, id, label) => {
    if (!confirm(`Delete "${label}"?`)) return;
    pushOps([{ op: `${kind}.del`, id }], 'Deleted');
  };
  const toggleAlarm = (a) => pushOps(
    [{ op: 'alarm.put', id: a.id, alarm: { ...a, enabled: !a.enabled } }],
    a.enabled ? 'Switched off' : 'Switched on'
  );
  const toggleTask = (t) => pushOps(
    [{ op: 'task.put', id: t.id, task: { ...t, done: !t.done } }],
    t.done ? 'Marked as not done' : 'Checked off'
  );

  // ---- render ----
  return (
    <main>
      <header className="top">
        <div>
          <h1>Planner</h1>
          <p className="sub">
            {counts.a} alarm{counts.a === 1 ? '' : 's'} · {counts.r} reminder{counts.r === 1 ? '' : 's'} · {counts.t} task{counts.t === 1 ? '' : 's'}
            {' '}{state && `· ${counts.on} active · synced (r${state.rev})`}
            {syncing && ' · syncing…'}
          </p>
        </div>
        <div className="btns">
          <button className="btn" onClick={() => setModal({ type: 'task', entry: null, presetDate: selDate })}>+ Task</button>
          <button className="btn" onClick={() => setModal({ type: 'reminder', entry: null, presetDate: selDate })}>+ Reminder</button>
          <button className="btn pri" onClick={() => setModal({ type: 'alarm', entry: null, presetDate: selDate })}>+ Alarm</button>
        </div>
      </header>

      <div className="seg">
        <button className={view === 'cal' ? 'on' : ''} onClick={() => setView('cal')}>Calendar</button>
        <button className={view === 'list' ? 'on' : ''} onClick={() => setView('list')}>List</button>
      </div>

      {view === 'cal' && (
        <div className="cols">
          <section className="card">
            <div className="cal-nav">
              <button className="ib" onClick={() => (calM === 1 ? (setCalM(12), setCalY(calY - 1)) : setCalM(calM - 1))}>‹</button>
              <b>{MONFULL[calM - 1]} {calY}</b>
              <button className="ib" onClick={() => (calM === 12 ? (setCalM(1), setCalY(calY + 1)) : setCalM(calM + 1))}>›</button>
            </div>
            <div className="cal-dow">{DAYS.map((d) => <span key={d}>{d}</span>)}</div>
            <div className="cal-grid">
              {cells.map((c) => (
                <button
                  key={c.iso}
                  className={`cal-d${c.inMonth ? '' : ' out'}${c.isToday ? ' today' : ''}${c.iso === selDate ? ' sel' : ''}`}
                  onClick={() => setSelDate(c.iso)}
                >
                  <b>{c.d}</b>
                  <span className="dots">
                    {Array.from({ length: Math.min(c.tasks.filter((t) => !t.done).length, 3) }).map((_, i) => <i key={`t${i}`} className="t" />)}
                    {Array.from({ length: Math.min(c.tasks.filter((t) => t.done).length, 2) }).map((_, i) => <i key={`d${i}`} className="t d" />)}
                    {Array.from({ length: Math.min(c.alarms.length, 2) }).map((_, i) => <i key={`a${i}`} />)}
                    {c.tasks.length + c.alarms.length > 5 && <em>+{c.tasks.length + c.alarms.length - 5}</em>}
                  </span>
                </button>
              ))}
            </div>
            <div className="legend">
              <span><i />alarm / reminder</span>
              <span><i className="t" />task</span>
              <span><i className="t d" />done</span>
            </div>
          </section>

          <section className="card">
            <h2>{dayTitle(selDate)}</h2>
            <p className="sub">{dayTasks.length} task{dayTasks.length === 1 ? '' : 's'} · {dayAlarms.length} alarm{dayAlarms.length === 1 ? '' : 's'} this day</p>
            <div className="rows">
              {dayTasks.map((t) => (
                <Row key={t.id}>
                  <button className={`ck${t.done ? ' on' : ''}`} disabled={busy} onClick={() => toggleTask(t)}>✓</button>
                  <div className="grow">
                    <b className={t.done ? 'strike' : ''}>{t.title}</b>
                    <span className="dim">{taskText(t)}</span>
                  </div>
                  <button className="ib" onClick={() => setModal({ type: 'task', entry: t })}>✎</button>
                  <button className="ib" onClick={() => delEntry('task', t.id, t.title)}>🗑</button>
                </Row>
              ))}
              {dayAlarms.map((a) => (
                <Row key={a.id}>
                  <span className="badge r">{a.type === 'reminder' ? 'reminder' : 'alarm'}</span>
                  <div className="grow">
                    <b>{fmtTime(a.time)} {a.label || (a.type === 'reminder' ? 'Reminder' : 'Alarm')}</b>
                    <span className="dim">{kindText(a)}</span>
                  </div>
                  <button className="ib" onClick={() => setModal({ type: a.type, entry: a })}>✎</button>
                  <button className="ib" onClick={() => delEntry('alarm', a.id, a.label || a.type)}>🗑</button>
                </Row>
              ))}
              {!dayTasks.length && !dayAlarms.length && <div className="empty">Nothing on this day.</div>}
            </div>
            <div className="btns left">
              <button className="btn sm" onClick={() => setModal({ type: 'task', entry: null, presetDate: selDate })}>+ Task</button>
              <button className="btn sm" onClick={() => setModal({ type: 'reminder', entry: null, presetDate: selDate })}>+ Reminder</button>
              <button className="btn sm" onClick={() => setModal({ type: 'alarm', entry: null, presetDate: selDate })}>+ Alarm</button>
            </div>
          </section>
        </div>
      )}

      {view === 'list' && (
        <section className="card">
          <div className="chips">
            {[['all', 'All'], ['alarm', 'Alarms'], ['reminder', 'Reminders'], ['task', 'Tasks']].map(([k, l]) => (
              <button key={k} className={`chip${filter === k ? ' on' : ''}`} onClick={() => setFilter(k)}>{l}</button>
            ))}
          </div>
          <div className="rows">
            {filter !== 'task' && alarms
              .filter((a) => filter === 'all' || a.type === filter)
              .sort((x, y) => (x.time < y.time ? -1 : 1))
              .map((a) => (
                <Row key={a.id} className={a.enabled ? '' : 'off'}>
                  <span className="badge r">{a.type}</span>
                  <div className="grow">
                    <b>{fmtTime(a.time)} {a.label || a.type}</b>
                    <span className="dim">{kindText(a)} · {a.enabled ? 'on' : 'off'}</span>
                  </div>
                  <label className="sw">
                    <input type="checkbox" checked={!!a.enabled} disabled={busy} onChange={() => toggleAlarm(a)} />
                    <i />
                  </label>
                  <button className="ib" onClick={() => setModal({ type: a.type, entry: a })}>✎</button>
                  <button className="ib" onClick={() => delEntry('alarm', a.id, a.label || a.type)}>🗑</button>
                </Row>
              ))}
            {filter === 'all' && tasks.length > 0 && <div className="group">Tasks</div>}
            {(filter === 'all' || filter === 'task') && [...tasks]
              .sort((x, y) => dkey(x.date) - dkey(y.date))
              .map((t) => (
                <Row key={t.id} className={t.done ? 'off' : ''}>
                  <button className={`ck${t.done ? ' on' : ''}`} disabled={busy} onClick={() => toggleTask(t)}>✓</button>
                  <div className="grow">
                    <b className={t.done ? 'strike' : ''}>{t.title}</b>
                    <span className="dim">{taskText(t)}</span>
                  </div>
                  <button className="ib" onClick={() => setModal({ type: 'task', entry: t })}>✎</button>
                  <button className="ib" onClick={() => delEntry('task', t.id, t.title)}>🗑</button>
                </Row>
              ))}
            {((filter === 'task' || filter === 'all') && !tasks.length && !alarms.length) &&
              <div className="empty">Nothing scheduled yet.<br />Add an alarm, a reminder or a task above.</div>}
          </div>
        </section>
      )}

      {modal && (
        <Modal onClose={() => setModal(null)}>
          {modal.type === 'task'
            ? <TaskForm entry={modal.entry} presetDate={modal.presetDate || selDate} busy={busy}
                onSave={saveTask} onClose={() => setModal(null)} />
            : <AlarmForm entry={modal.entry} type={modal.type} presetDate={modal.presetDate || selDate} busy={busy}
                onSave={saveAlarm} onClose={() => setModal(null)} />}
        </Modal>
      )}

      {toast && <div className={`toast${toast.err ? ' err' : ''}`}>{toast.msg}</div>}
      {!state && !syncing && (
        <div className="toast err">Not connected - is the planner API up?</div>
      )}
    </main>
  );
}

// ---------------- presentational bits ----------------

function score(t) {
  return (t.done ? 2e6 : 0) + (t.time ? +t.time.slice(0, 2) * 60 + +t.time.slice(3, 5) : 1e5);
}
function taskText(t) {
  const range = (t.end && t.end !== t.date) ? `${shortDate(t.date)} – ${shortDate(t.end)}` : shortDate(t.date);
  return range + (t.time ? ` · ${fmtTime(t.time)}` : '') + (t.done ? ' · done' : '');
}
function kindText(a) {
  if (a.kind === 'daily') return 'Every day';
  if (a.kind === 'weekly') {
    const d = [];
    for (let i = 0; i < 7; i++) if (a.weekdays & (1 << i)) d.push(DAYS[i]);
    return d.length === 7 ? 'Every day' : d.join(' ');
  }
  if (a.kind === 'once') return a.date;
  return `Countdown · ${a.date}`;
}
function dayTitle(iso) {
  const d = new Date(iso + 'T12:00:00');
  const t = todayISO();
  return `${DOW[d.getDay()]}, ${d.getDate()} ${MONFULL[d.getMonth()]}${iso === t ? ' · today' : ''}`;
}

function Row({ children, className = '' }) {
  return <div className={`row-item ${className}`}>{children}</div>;
}

function Modal({ children, onClose }) {
  return (
    <div className="overlay" onClick={(e) => e.target === e.currentTarget && onClose()}>
      <div className="modal">{children}</div>
    </div>
  );
}

function TaskForm({ entry, presetDate, busy, onSave, onClose }) {
  const [title, setTitle] = useState(entry?.title || '');
  const [date, setDate] = useState(entry?.date || presetDate || todayISO());
  const [multi, setMulti] = useState(!!entry?.end);
  const [end, setEnd] = useState(entry?.end || presetDate || todayISO());
  const [hasTime, setHasTime] = useState(!!entry?.time);
  const [time, setTime] = useState(entry?.time || '09:00');
  return (
    <form onSubmit={(e) => {
      e.preventDefault();
      if (!title.trim()) return;
      if (multi && end < date) return;
      onSave({
        id: entry?.id || undefined,
        title: title.trim(),
        date,
        end: multi ? end : '',
        time: hasTime ? time : '',
        done: entry?.done || false,
      });
      onClose();
    }}>
      <h3>{entry ? 'Edit task' : 'New task'}</h3>
      <label>Title
        <input value={title} onChange={(e) => setTitle(e.target.value)} maxLength={23} autoFocus required
          placeholder="What should happen?" />
      </label>
      <label>Day
        <input type="date" value={date} onChange={(e) => { setDate(e.target.value); if (end < e.target.value) setEnd(e.target.value); }} required />
      </label>
      <label className="chk"><input type="checkbox" checked={multi} onChange={(e) => setMulti(e.target.checked)} /> runs over several days</label>
      {multi && (
        <label>Last day
          <input type="date" value={end} min={date} onChange={(e) => setEnd(e.target.value)} required />
        </label>
      )}
      <label className="chk"><input type="checkbox" checked={hasTime} onChange={(e) => setHasTime(e.target.checked)} /> has a time (ordering only, never rings)</label>
      {hasTime && (
        <label>Time
          <input type="time" value={time} onChange={(e) => setTime(e.target.value)} required />
        </label>
      )}
      <div className="btns">
        <button type="button" className="btn" onClick={onClose}>Cancel</button>
        <button type="submit" className="btn pri" disabled={busy}>Save task</button>
      </div>
    </form>
  );
}

function AlarmForm({ entry, type, presetDate, busy, onSave, onClose }) {
  const isReminder = (entry?.type || type) === 'reminder';
  const [t, setT] = useState(entry?.type || type || 'alarm');
  const [kind, setKind] = useState(entry?.kind || (isReminder ? 'once' : 'daily'));
  const [time, setTime] = useState(entry?.time || '07:00');
  const [date, setDate] = useState(entry?.date || presetDate || todayISO());
  const [weekdays, setWeekdays] = useState(entry?.kind === 'weekly' ? entry.weekdays : 127);
  const [minutes, setMinutes] = useState(10);
  const [label, setLabel] = useState(entry?.label || '');
  const [melody, setMelody] = useState(entry?.melody ?? (isReminder ? 6 : 0));
  const tones = t === 'reminder' ? REMINDER_TONES : ALARM_TONES;
  return (
    <form onSubmit={(e) => {
      e.preventDefault();
      if (kind === 'once' && !date) return;
      if (kind === 'weekly' && !weekdays) return;
      if (kind === 'countdown' && !(minutes >= 1)) return;
      const out = {
        id: entry?.id || undefined,
        enabled: true,
        type: t,
        kind,
        time,
        date: kind === 'once' || kind === 'countdown' ? date : '',
        weekdays: kind === 'weekly' ? weekdays : 0,
        melody,
        label: label.trim(),
      };
      // countdown: the client computes the absolute date/time (the device
      // clock is the one that rings, so we anchor to *this* machine's now)
      if (kind === 'countdown') {
        const d = new Date(Date.now() + minutes * 60000);
        out.date = isoOf(d);
        out.time = `${pad2(d.getHours())}:${pad2(d.getMinutes())}`;
      }
      onSave(out);
      onClose();
    }}>
      <h3>{entry ? `Edit ${t}` : `New ${t}`}</h3>
      <div className="seg">
        <button type="button" className={t === 'alarm' ? 'on' : ''} onClick={() => { setT('alarm'); setMelody(0); }}>Alarm</button>
        <button type="button" className={t === 'reminder' ? 'on' : ''} onClick={() => { setT('reminder'); setMelody(6); }}>Reminder</button>
      </div>
      <div className="seg">
        {['once', 'daily', 'weekly', 'countdown'].map((k) => (
          <button type="button" key={k} className={kind === k ? 'on' : ''} onClick={() => setKind(k)}>{k}</button>
        ))}
      </div>
      {kind !== 'countdown' && (
        <label>Time
          <input type="time" value={time} onChange={(e) => setTime(e.target.value)} required />
        </label>
      )}
      {kind === 'once' && (
        <label>Date
          <input type="date" value={date} onChange={(e) => setDate(e.target.value)} required />
        </label>
      )}
      {kind === 'weekly' && (
        <div className="days">
          {DAYS.map((d, i) => (
            <button type="button" key={d} className={(weekdays & (1 << i)) ? 'on' : ''}
              onClick={() => setWeekdays(weekdays ^ (1 << i))}>{d}</button>
          ))}
        </div>
      )}
      {kind === 'countdown' && (
        <label>In (minutes)
          <input type="number" min="1" max="527000" value={minutes}
            onChange={(e) => setMinutes(+e.target.value)} required />
        </label>
      )}
      <label>{t === 'reminder' ? 'Message shown on the display' : 'Label'}
        <input value={label} onChange={(e) => setLabel(e.target.value)} maxLength={31}
          placeholder={t === 'reminder' ? 'Drink water' : 'Wake up'} />
      </label>
      <label>Tone
        <select value={tones.includes(+melody) ? melody : tones[0]} onChange={(e) => setMelody(+e.target.value)}>
          {tones.map((i) => <option key={i} value={i}>{MELS[i]}</option>)}
        </select>
      </label>
      <div className="btns">
        <button type="button" className="btn" onClick={onClose}>Cancel</button>
        <button type="submit" className="btn pri" disabled={busy}>Save {t}</button>
      </div>
    </form>
  );
}
