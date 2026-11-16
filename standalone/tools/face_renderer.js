/* ============================================================
   Desk Buddy face renderer (JS twin of src/faces.cpp + animations.cpp)
   Same maths, same spans-only rasteriser => same pixels as the OLED.
   Tables (EXPR / PACKS / MOODS) are prepended by tools/gen_faces.py
   ============================================================ */
const NP = 10, PW = 0, PH = 1, PR = 2, PDX = 3, PDY = 4, PLID = 5, PLIDB = 6, PSL = 7, PCHK = 8, PSH = 9;
const NEU = [24, 30, .45, 22, 0, 0, 0, 0, 0, .5];
const F_JIGGLE = 1, F_TREMBLE = 2, F_WOBBLE = 4, F_LOOK_SIDE = 8, F_LOOK_UP = 16, F_AVERT = 32;
const PK_BOXY = 0, PK_ORB = 1, PK_MORPH = 2, PK_PIXEL = 3, PK_SLEEK = 4, PK_CAT = 5;
const rnd = x => x < 0 ? -Math.round(-x) : Math.round(x);
const q64 = x => rnd(x * 64) / 64;
const clampf = (v, a, b) => v < a ? a : (v > b ? b : v);

/* ---------- rig ---------- */
function makeRig() {
  return {
    lookTX: 0, lookTY: 0, lx: 0, lv: 0, ly: 0, lyv: 0, sq: 1, sqv: 0,
    blink: 0, blinkT: 0, blinkSpeed: 1, bph: 0, by: 0, bamp: 1, wob: 0, wph: 0, tempo: 1,
    cur: [NEU.slice(), NEU.slice()], vel: [new Array(NP).fill(0), new Array(NP).fill(0)],
    tgt: [NEU.slice(), NEU.slice()], gl: 0, glv: 0, glT: 0, ln: 0, lnv: 0, lnT: 0, mouth: 0, mouthT: 0, ms: 1, msv: 0, blush: 0, blushT: 0, glyph: 0, glyphT: 0, acc: 0, flags: 0,
    t: 0, spin: 0
  };
}
function rigCloseEyes(r) { for (let e = 0; e < 2; e++) { r.cur[e][PH] = 2; r.vel[e][PH] = 0; } }
function rigImpulse(r, a) { r.sqv += a * 6; }
function rigLookAt(r, x, y) { r.lookTX = clampf(x, -1.3, 1.3); r.lookTY = clampf(y, -1.3, 1.3); }
function rigBlink(r, dt) {
  const sp = r.blinkSpeed;
  if (r.blinkT > r.blink) { r.blink += dt * 14 * sp; if (r.blink >= 1) { r.blink = 1; r.blinkT = 0; } }
  else if (r.blink > 0) { r.blink -= dt * 9 * sp; if (r.blink < 0) r.blink = 0; }
}
function rigTick(r, dt) {
  if (dt > .05) dt = .05;
  r.t += dt; r.spin += dt * 6;
  const tp = r.tempo, k = 125 * tp * tp, d = 13.8 * tp;
  const swapping = r.glyph !== r.glyphT;
  for (let e = 0; e < 2; e++) for (let p = 0; p < NP; p++) {
    let target = r.tgt[e][p];
    if (swapping && p === PH) target = 2;
    const a = (target - r.cur[e][p]) * k - r.vel[e][p] * d;
    r.vel[e][p] += a * dt; r.cur[e][p] += r.vel[e][p] * dt;
  }
  if (swapping && r.cur[0][PH] < 6) r.glyph = r.glyphT;
  const lk = 120 * tp * tp, ld = 15 * tp;
  r.lv += ((r.lookTX - r.lx) * lk - r.lv * ld) * dt; r.lx += r.lv * dt;
  r.lyv += ((r.lookTY - r.ly) * lk - r.lyv * ld) * dt; r.ly += r.lyv * dt;
  r.sqv += ((1 - r.sq) * 170 - r.sqv * 13) * dt; r.sq += r.sqv * dt;
  r.sq = clampf(r.sq, .55, 1.5);
  r.lnv += ((r.lnT - r.ln) * 60 - r.lnv * 9) * dt; r.ln += r.lnv * dt;
  { const tg = (r.mouth === r.mouthT) ? 1 : 0;
    r.msv += ((tg - r.ms) * 260 - r.msv * 22) * dt; r.ms += r.msv * dt;
    if (r.mouth !== r.mouthT && r.ms < .18) r.mouth = r.mouthT; }
  r.glv += ((r.glT - r.gl) * 90 - r.glv * 11) * dt; r.gl += r.glv * dt;
  r.bph += dt * 1.6 * tp; r.by = Math.sin(r.bph) * r.bamp;
  r.wph += dt * 14;
  if (r.wob > 0) { r.wob -= dt * .45; if (r.wob < 0) r.wob = 0; }
  r.blush += (r.blushT - r.blush) * Math.min(1, dt * 6);
}
function rigSetFace(r, e, pack, mood) {
  if (e < 0 || e >= EXPR.length) e = 0;
  const x = EXPR[e], m = MOODS[mood] || MOODS[0], k = m.ex;
  for (let s = 0; s < 2; s++) {
    const inn = s ? x[2] : x[1];     // [w,h,r,dx,dy,lid,lidB,slope,cheek,shine]
    let w = 24 + (inn[0] - 24) * k, h = 30 + (inn[1] - 30) * k, dy = inn[4] * k;
    const lid = clampf(inn[5] * k, 0, .85), lidB = clampf(inn[6] * k, 0, .7);
    const slope = clampf(inn[7] * k, -1, 1), cheek = clampf(inn[8] * k, 0, 1);
    let dx = inn[3], frac = inn[2], shineK = 1;
    const rr = inn[2], shine = inn[9];
    if (x[3] === GL_NONE) switch (pack) {
      case PK_BOXY: frac = Math.min(.85, rr * 1.1); shineK = (shine >= .8) ? .5 : 0; break;
      case PK_ORB:
        if (h >= 14) { const sz = (w + h) * .5; w += (sz - w) * .75; h += (sz - h) * .75; }
        frac = 1; shineK = 1; break;
      case PK_MORPH: frac = .22 + .78 * (x[7] / 100); shineK = (shine >= .8) ? .8 : 0; break;
      case PK_PIXEL: frac = 0; shineK = 0; break;
      case PK_SLEEK: w *= 1.25; h = Math.max(3, h * .55); frac = 1; dx += 3; shineK = 0; break;
      case PK_CAT: w *= .8; h *= 1.12; frac = 1; shineK = 1; break;
    }
    r.tgt[s] = [w, h, frac, dx, dy, lid, lidB, slope, cheek, shine * shineK];
  }
  r.glyphT = x[3]; r.mouthT = x[8]; r.acc = x[5]; r.flags = x[6]; r.blushT = x[4];
  r.tempo = m.tp; r.blinkSpeed = m.bs; r.bamp = m.bo;
}

/* ---------- framebuffer: spans only, same as firmware ---------- */
class Fb {
  constructor() { this.b = new Uint8Array(128 * 64); this.c = 1; }
  clear() { this.b.fill(0); }
  px(x, y) { if (x < 0 || x > 127 || y < 0 || y > 63) return; this.b[y * 128 + x] = this.c; }
  span(x0, x1, y) {
    if (y < 0 || y > 63 || x0 > x1) return;
    if (x0 < 0) x0 = 0; if (x1 > 127) x1 = 127; if (x0 > x1) return;
    this.b.fill(this.c, y * 128 + x0, y * 128 + x1 + 1);
  }
  rbox(x, y, w, h, rad) {
    const mx = (Math.min(w, h) - 1) >> 1;
    if (rad > mx) rad = mx; if (rad < 0) rad = 0;
    for (let i = 0; i < h; i++) {
      const d = Math.min(i, h - 1 - i); let inset = 0;
      if (d < rad) { const t = rad - d - .5; inset = rnd(rad - Math.sqrt(Math.max(0, rad * rad - t * t))); }
      this.span(x + inset, x + w - 1 - inset, y + i);
    }
  }
  ellipse(cx, cy, rx, ry) {
    if (rx < 1) rx = 1; if (ry < 1) ry = 1;
    for (let yy = -ry; yy <= ry; yy++) {
      const t = yy / (ry + .5), hx = (rx + .5) * Math.sqrt(Math.max(0, 1 - t * t)), ix = Math.floor(hx);
      this.span(cx - ix, cx + ix, cy + yy);
    }
  }
  tri(x0, y0, x1, y1, x2, y2) {
    const xs = [x0, x1, x2], ys = [y0, y1, y2];
    const ymin = Math.min(y0, y1, y2), ymax = Math.max(y0, y1, y2);
    for (let y = ymin; y <= ymax; y++) {
      const yc = y + .5; let lo = 1e9, hi = -1e9, n = 0;
      for (let i = 0; i < 3; i++) {
        const j = (i + 1) % 3, ya = ys[i], yb = ys[j];
        if ((ya <= yc && yc < yb) || (yb <= yc && yc < ya)) {
          const xv = xs[i] + (yc - ya) * (xs[j] - xs[i]) / (yb - ya);
          if (xv < lo) lo = xv; if (xv > hi) hi = xv; n++;
        }
      }
      if (n >= 2) this.span(Math.ceil(lo - .5), Math.floor(hi - .5), y);
    }
  }
  line(x0, y0, x1, y1) {
    const dx = Math.abs(x1 - x0), sx = x0 < x1 ? 1 : -1, dy = -Math.abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    let err = dx + dy;
    for (; ;) {
      this.px(x0, y0);
      if (x0 === x1 && y0 === y1) break;
      const e2 = 2 * err;
      if (e2 >= dy) { err += dy; x0 += sx; }
      if (e2 <= dx) { err += dx; y0 += sy; }
    }
  }
}

function drawHeart(g, ex, ey, iw, ih, t, beat) {
  const b = 1 + .10 * Math.max(0, Math.sin(t * beat)), W = iw * b, H = ih * b;
  let rl = rnd(W * .27); if (rl < 2) rl = 2;
  const lx = rnd(ex - W * .235), rx = rnd(ex + W * .235), ly = rnd(ey - H * .13);
  g.ellipse(lx, ly, rl, rl); g.ellipse(rx, ly, rl, rl);
  const top = rnd(ey - H * .10), tip = rnd(ey + H * .50);
  g.tri(ex - rnd(W * .50), top, ex + rnd(W * .50), top, ex, tip);
  if (W >= 14) { g.c = 0; g.ellipse(lx - Math.trunc(rl / 3), ly - Math.trunc(rl / 3), 1, 1); g.c = 1; }
}
function drawFlame(g, ex, ey, iw, ih, t) {
  const W = iw, H = ih;
  const fl1 = Math.sin(t * 13) * .10 + Math.sin(t * 21) * .05, fl2 = Math.sin(t * 17 + 1) * .08, hs = 1 + .08 * Math.sin(t * 15);
  g.ellipse(ex, ey + rnd(H * .17), rnd(W * .36), rnd(H * .33));
  g.tri(rnd(ex - W * .32), rnd(ey + H * .10), rnd(ex + W * .32), rnd(ey + H * .10), rnd(ex + W * fl1), rnd(ey - H * .52 * hs));
  g.tri(rnd(ex - W * .36), rnd(ey + H * .12), rnd(ex - W * .05), rnd(ey - H * .02), rnd(ex - W * .30 + W * fl2), rnd(ey - H * .30));
  g.tri(rnd(ex + W * .05), rnd(ey - H * .02), rnd(ex + W * .36), rnd(ey + H * .12), rnd(ex + W * .30 - W * fl2), rnd(ey - H * .32));
  g.c = 0;
  g.ellipse(ex, ey + rnd(H * .24), rnd(W * .20), rnd(H * .19));
  g.tri(rnd(ex - W * .17), rnd(ey + H * .22), rnd(ex + W * .17), rnd(ey + H * .22), rnd(ex + W * fl1 * .6), rnd(ey - H * .18 * hs));
  g.c = 1;
  g.ellipse(ex, ey + rnd(H * .30), rnd(W * .08), rnd(H * .09));
}
function drawChevron(g, side, ex, ey, iw, ih) {
  const hw = Math.max(3, rnd(iw * .40)), hh = Math.max(3, rnd(ih * .36));
  const tipx = side < 0 ? ex + hw : ex - hw, backx = side < 0 ? ex - hw : ex + hw;
  const tk = iw >= 20 ? 3 : 2;
  for (let o = 0; o < tk; o++) { g.line(backx, ey - hh + o, tipx, ey + o); g.line(tipx, ey + o, backx, ey + hh + o); }
}
function drawPaw(g, ex, ey, iw, ih) {
  g.ellipse(ex, ey + rnd(ih * .08), rnd(iw * .42), rnd(ih * .38));
  const r = Math.max(1, rnd(iw * .11));
  g.ellipse(ex - rnd(iw * .25), ey - rnd(ih * .34), r, r); g.ellipse(ex + rnd(iw * .25), ey - rnd(ih * .34), r, r);
  g.c = 0; g.ellipse(ex - rnd(iw * .12), ey - rnd(ih * .02), 1, 1); g.c = 1;
}
function drawArch(g, ex, ey, iw, ih) {
  const hw = Math.max(4, (iw >> 1) - 1), ah = Math.max(3, rnd(ih * .38)), th = ih >= 18 ? 3 : 2;
  for (let x = -hw; x <= hw; x++) { const u = x / hw, y = rnd(ey + ah * .5 - ah * (1 - u * u)); for (let k = 0; k < th; k++) g.px(ex + x, y + k); }
}

function drawEye(g, side, p, w, h, exf, eyf, pack, glyph, gx, spin, tt, beat) {
  let iw = rnd(w), ih = rnd(h);
  if (iw < 2) iw = 2; if (ih < 2) ih = 2;
  let ex = rnd(exf), ey = rnd(eyf);
  if (pack === PK_PIXEL) { iw &= ~1; ih &= ~1; ex &= ~1; ey &= ~1; if (iw < 2) iw = 2; if (ih < 2) ih = 2; }
  const x0 = ex - (iw >> 1), y0 = ey - (ih >> 1), x1 = x0 + iw - 1, y1 = y0 + ih - 1;
  const bx = { x0, y0, x1, y1, ex, ey };
  g.c = 1;
  if (glyph === 2) {
    const hv = Math.max(3, Math.min(iw, ih) >> 1);
    for (let o = 0; o <= 1; o++) { g.line(ex - hv + o, ey - hv, ex + hv + o, ey + hv); g.line(ex - hv + o, ey + hv, ex + hv + o, ey - hv); }
    return bx;
  }
  if (glyph === GL_HEART)   { drawHeart(g, ex, ey, iw, ih, tt, beat); return bx; }
  if (glyph === GL_FLAME)   { drawFlame(g, ex, ey, iw, ih, tt);       return bx; }
  if (glyph === GL_CHEVRON) { drawChevron(g, side, ex, ey, iw, ih);   return bx; }
  if (glyph === GL_PAW)     { drawPaw(g, ex, ey, iw, ih);             return bx; }
  if (glyph === GL_ARCH)    { drawArch(g, ex, ey, iw, ih);            return bx; }
  if (glyph === 1) {
    const R = Math.min(iw, ih) / 2, n = 36; let lx = 0, ly = 0;
    for (let i = 0; i <= n; i++) {
      const u = i / n, ang = u * 3 * 6.2832 + spin, rad = u * R;
      const x = rnd(ex + Math.cos(ang) * rad), y = rnd(ey + Math.sin(ang) * rad);
      if (i > 0) g.line(lx, ly, x, y);
      lx = x; ly = y;
    }
    return bx;
  }
  const ell = (pack === PK_ORB || pack === PK_CAT || p[PR] >= .97);
  if (ell) g.ellipse(ex, ey, iw >> 1, ih >> 1);
  else g.rbox(x0, y0, iw, ih, pack === PK_PIXEL ? 0 : rnd(p[PR] * Math.min(iw, ih) / 2));

  const sh = p[PSH];
  if (pack === PK_CAT) {
    if (ih >= 10) {
      const pw = 2 + Math.trunc(sh * 4), ph = Math.trunc(ih * .85);
      g.c = 0; g.rbox(ex - (pw >> 1) + rnd(gx * .5), ey - (ph >> 1), pw, ph, pw >> 1); g.c = 1;
    }
  } else if (sh > .12 && ih >= 12 && iw >= 12) {
    const sr = Math.trunc(1.5 + sh * 3);
    g.c = 0; g.ellipse(ex - (iw >> 2) + rnd(gx * .6), ey - (ih >> 2), sr, sr); g.c = 1;
  }

  g.c = 0;
  const lid = p[PLID], sl = p[PSL];
  if (lid > .01 || Math.abs(sl) > .01) {
    const yo = y0 + ih * (lid - sl * .45), yi = y0 + ih * (lid + sl * .45);
    const yl = side < 0 ? yo : yi, yr = side < 0 ? yi : yo;
    const Yl = rnd(clampf(yl, y0 - 1, y1 + 1)), Yr = rnd(clampf(yr, y0 - 1, y1 + 1)), ya = y0 - 3;
    g.tri(x0 - 2, ya, x1 + 3, ya, x1 + 3, Yr);
    g.tri(x0 - 2, ya, x1 + 3, Yr, x0 - 2, Yl);
  }
  const lb = p[PLIDB];
  if (lb > .01) { const yb = y1 - rnd(lb * ih) + 1; for (let y = yb; y <= y1 + 2; y++) g.span(x0 - 2, x1 + 2, y); }
  const ck = p[PCHK];
  if (ck > .02) {
    const ry = ih * .65, rx = iw * .75, ccy = y1 + ry + 2 - ck * ih * .95;
    g.ellipse(ex, rnd(ccy), rnd(rx), rnd(ry));
  }
  g.c = 1;
  return bx;
}

function drop(g, x, y) { g.px(x, y - 2); g.span(x - 1, x + 1, y - 1); g.span(x - 1, x + 1, y); g.px(x, y + 1); }
function zglyph(g, x, y, s) { g.span(x, x + s, y); g.line(x + s, y, x, y + s); g.span(x, x + s, y + s); }

function insetAt(i, h, rad) {
  const d = Math.min(i, h - 1 - i);
  if (d >= rad) return 0;
  const t = rad - d - .5;
  return rnd(rad - Math.sqrt(Math.max(0, rad * rad - t * t)));
}
function frameRBox(g, x, y, w, h, rad, th) {
  const mx = (Math.min(w, h) - 1) >> 1;
  if (rad > mx) rad = mx; if (rad < 0) rad = 0;
  let irad = rad - th; if (irad < 0) irad = 0;
  const iw = w - 2 * th, ih = h - 2 * th;
  for (let i = 0; i < h; i++) {
    const ins = insetAt(i, h, rad), xl = x + ins, xr = x + w - 1 - ins;
    if (i < th || i >= h - th || iw <= 0 || ih <= 0) { g.span(xl, xr, y + i); continue; }
    const ii = insetAt(i - th, ih, irad), il = x + th + ii, ir = x + th + iw - 1 - ii;
    g.span(xl, il - 1, y + i); g.span(ir + 1, xr, y + i);
  }
}
function drawGlasses(g, gl, cx, cy, sc) {
  if (gl < .03) return;
  const lw = rnd(38 * sc), lh = rnd(40 * sc), rad = rnd(10 * sc), th = sc < .75 ? 1 : 2;
  let yo = -(1 - gl) * 50 * sc; if (yo > 6 * sc) yo = 6 * sc;
  const yoff = rnd(yo), dxl = rnd(22 * sc);
  const xL = cx - dxl - (lw >> 1), xR = cx + dxl - (lw >> 1), y0 = cy + yoff - (lh >> 1);
  frameRBox(g, xL, y0, lw, lh, rad, th); frameRBox(g, xR, y0, lw, lh, rad, th);
  const yb = cy + yoff - rnd(6 * sc);
  for (let k = 0; k < th; k++) g.span(xL + lw - 1, xR, yb + k);
  const ty = cy + yoff - rnd(8 * sc), ty2 = cy + yoff - rnd(13 * sc);
  for (let k = 0; k < th; k++) { g.line(xL, ty + k, 0, ty2 + k); g.line(xR + lw - 1, ty + k, 127, ty2 + k); }
}

function arcMouth(g, cx, my, hw, depth, th, smile) {
  if (hw < 1) hw = 1;
  for (let x = -hw; x <= hw; x++) { const u = x / hw, off = smile ? depth * (1 - u * u) : depth * u * u, y = my + rnd(off); for (let k = 0; k < th; k++) g.px(cx + x, y + k); }
}
function halfDisc(g, cx, my, hw, hh) {
  if (hh < 1) hh = 1;
  for (let i = 0; i <= hh; i++) { const u = i / (hh + .5), hx = Math.floor((hw + .5) * Math.sqrt(Math.max(0, 1 - u * u))); g.span(cx - hx, cx + hx, my + i); }
}
const KISS3 = ['.###.', '#...#', '....#', '..##.', '....#', '#...#', '.###.'];
function drawMouth(g, type, s, cx, my, t) {
  if (type === MO_NONE || s < .25) return;
  const th = s >= .8 ? 2 : 1;
  switch (type) {
    case MO_SMILE: arcMouth(g, cx, my, rnd(6 * s), rnd(3 * s), th, true); break;
    case MO_TINY: arcMouth(g, cx, my + 1, rnd(3 * s), rnd(1 * s), 1, true); break;
    case MO_FROWN: arcMouth(g, cx, my, rnd(6 * s), rnd(3 * s), th, false); break;
    case MO_POUT: arcMouth(g, cx, my + 1, rnd(3 * s), rnd(2 * s), th, false); break;
    case MO_FLAT: for (let k = 0; k < th; k++) g.span(cx - rnd(5 * s), cx + rnd(5 * s), my + 1 + k); break;
    case MO_OMEGA:
      arcMouth(g, cx - rnd(3 * s), my, rnd(3 * s), rnd(2 * s), th, true);
      arcMouth(g, cx + rnd(3 * s), my, rnd(3 * s), rnd(2 * s), th, true); break;
    case MO_GRIN: halfDisc(g, cx, my - 1, rnd(7 * s), rnd(6 * s)); break;
    case MO_LAUGH: halfDisc(g, cx, my - 1, rnd(9 * s), rnd(8 * s + 1.2 * Math.sin(t * 22))); break;
    case MO_WAIL: g.ellipse(cx, my + rnd(3 * s), rnd(7 * s), rnd((3 + 1.4 * Math.sin(t * 20)) * s)); break;
    case MO_O: g.ellipse(cx, my + rnd(2 * s), rnd(3 * s), rnd(3 * s)); break;
    case MO_SNORE: {
      const br = .5 + .5 * Math.sin(t * 1.7);
      g.ellipse(cx, my + 2, rnd(2.5 * s), rnd((1.5 + 1.3 * br) * s));
      const ph = (t * .5) % 1; if (ph < .85) drop(g, cx + rnd(4 * s), my + rnd((3 + ph * 5) * s));
      break; }
    case MO_YAWN: {
      const pulse = .5 + .5 * Math.sin(t * 1.6), ry = rnd((1.5 + 5 * pulse) * s);
      g.ellipse(cx, my + ry, rnd(4 * s), ry); break; }
    case MO_SMIRK:
      for (let k = 0; k < th; k++) g.span(cx - rnd(5 * s), cx + rnd(2 * s), my + 1 + k);
      g.line(cx + rnd(2 * s), my + 1, cx + rnd(6 * s), my - 1); break;
    case MO_WAVY: {
      const hw = rnd(7 * s);
      for (let x = -hw; x <= hw; x++) { const u = x / (hw > 0 ? hw : 1), y = my + 1 + rnd(1.4 * s * Math.sin(u * 9.42 + t * 9)); for (let k = 0; k < th; k++) g.px(cx + x, y + k); }
      break; }
    case MO_TONGUE:
      arcMouth(g, cx, my, rnd(6 * s), rnd(2 * s), th, true);
      g.ellipse(cx + rnd(2 * s), my + rnd(5 * s), rnd(2.6 * s), rnd(3.2 * s));
      g.c = 0; g.px(cx + rnd(2 * s), my + rnd(4 * s)); g.px(cx + rnd(2 * s), my + rnd(5 * s)); g.c = 1; break;
    case MO_KISS: {
      const k = s >= 1.2 ? 2 : 1;
      for (let rr = 0; rr < 7; rr++) for (let cc = 0; cc < 5; cc++) if (KISS3[rr][cc] === '#')
        for (let dy = 0; dy < k; dy++) g.span(cx - 2 * k + cc * k, cx - 2 * k + cc * k + k - 1, my - 2 + rr * k + dy);
      break; }
  }
}

function drawFace(g, r, pack, cy, sc) {
  if (sc === undefined) sc = 1;
  g.c = 1;
  const cx = 64, fl = r.flags;
  const gl = clampf(r.gl, 0, 1), gk = 1 - .65 * gl, ln = clampf(r.ln, 0, 1.15);
  const gazeX = r.lx * (pack === PK_SLEEK ? 9 : 7) * sc * gk, gazeY = r.ly * 4.5 * sc * gk, sq = r.sq;
  const jig = (fl & F_JIGGLE) ? Math.sin(r.t * 18) * 1.5 : 0;
  const trem = (fl & F_TREMBLE) ? Math.sin(r.t * 60) : 0;
  const wob = ((fl & F_WOBBLE) ? Math.sin(r.t * 8) * 3 : 0) + Math.sin(r.wph) * r.wob * 4;
  const offx = (trem + wob) * sc, offy = (jig + r.by + ln * 3) * sc;
  const bl = r.glyph ? 0 : r.blink;
  const box = [];
  for (let s = 0; s < 2; s++) {
    const p = r.cur[s].map(q64);
    const w = p[PW] * sc * (1 + (1 - sq) * .5);
    let h = p[PH] * sc * sq * (1 - bl) * (1 - .10 * ln); if (h < 2) h = 2;
    const ex = cx + (s ? p[PDX] : -p[PDX]) * sc + gazeX + offx, ey = cy + p[PDY] * sc + gazeY + offy;
    box[s] = drawEye(g, s ? 1 : -1, p, w, h, ex, ey, pack, r.glyph, gazeX, r.spin, r.t, 8 + 5 * ln);
  }
  if (r.blush > .3) {
    const n = r.blush > 1.4 ? 3 : 2, by = cy + rnd(19 * sc);
    for (let sd = -1; sd <= 1; sd += 2) {
      const bx = cx + sd * rnd(30 * sc);
      for (let i = 0; i < n; i++) { const x = bx - n * 2 - 1 + i * 5; g.line(x, by + 3, x + 3, by); }
    }
  }
  {
    const GBF = [.5, .5, .5, .5, .5, .38, .46, .26];
    const hb = q64(Math.max(r.cur[0][PH], r.cur[1][PH])), dyb = q64(Math.max(r.cur[0][PDY], r.cur[1][PDY]));
    let bf = GBF[r.glyph < 8 ? r.glyph : 0];
    if (r.glyph === GL_NONE) bf -= .32 * q64(Math.max(r.cur[0][PCHK], r.cur[1][PCHK]));
    let my = cy + rnd((dyb + hb * bf + 7) * sc + offy);
    if (gl > .5) { const mn = cy + rnd(24 * sc); if (my < mn) my = mn; }
    const mx = cy + rnd(27 * sc); if (my > mx) my = mx;
    if (sc >= .45 && my < 60) drawMouth(g, r.mouth, q64(r.ms * sc), cx, my, r.t);
  }
  if (r.acc === AC_HEART) {
    const hs = rnd(10 * sc); drawHeart(g, cx + rnd(36 * sc), cy - rnd(19 * sc), hs, hs - 1, r.t, 7);
  } else if (r.acc === 1) {
    for (let s = 0; s < 2; s++) {
      const ph = (r.t * .75 + s * .4) % 1;
      if (ph < .9) {
        const iw = box[s].x1 - box[s].x0 + 1, tx = box[s].ex + (s ? 1 : -1) * ((iw >> 1) - 4);
        drop(g, tx, box[s].y1 - 2 + Math.trunc(ph * 18 * sc));
      }
    }
  } else if (r.acc === 2) {
    const ph = (r.t * .9) % 1;
    drop(g, box[1].x1 + 4, box[1].y0 - 2 + Math.trunc(ph * 14 * sc));
  } else if (r.acc === 3) {
    for (let k = 0; k < 2; k++) {
      const ph = (r.t * .4 + k * .5) % 1;
      if (ph < .85) zglyph(g, cx + rnd(22 * sc) + Math.trunc(ph * 12 * sc), cy - rnd(6 * sc) - Math.trunc(ph * 18 * sc), 3 + k);
    }
  }
  drawGlasses(g, r.gl, cx, cy, sc);
}

/* ---------- paint the 128x64 buffer to a canvas as an OLED ---------- */
function paintOled(cv, g, scale, gap, color) {
  const W = 128 * scale, H = 64 * scale;
  if (cv.width !== W) { cv.width = W; cv.height = H; }
  const c = cv.getContext('2d');
  c.fillStyle = '#04070b'; c.fillRect(0, 0, W, H);
  c.fillStyle = color || '#cfeaff';
  const s = scale - (gap ? 1 : 0);
  for (let y = 0; y < 64; y++) {
    let x = 0;
    while (x < 128) {
      if (!g.b[y * 128 + x]) { x++; continue; }
      let x2 = x; while (x2 < 128 && g.b[y * 128 + x2]) x2++;
      if (gap) { for (let k = x; k < x2; k++) c.fillRect(k * scale, y * scale, s, s); }
      else c.fillRect(x * scale, y * scale, (x2 - x) * scale, scale);
      x = x2;
    }
  }
}

/* settle a rig on a face and draw it once (static thumbnails) */
function renderSettled(face, pack, mood, cy, sc, glasses) {
  const r = makeRig(), g = new Fb();
  rigSetFace(r, face, pack, mood);
  if (glasses) r.glT = 1;
  r.glyph = r.glyphT; r.blush = r.blushT;
  for (let i = 0; i < 100; i++) rigTick(r, .033);
  r.t = 0.2;
  g.clear(); drawFace(g, r, pack, cy === undefined ? 32 : cy, sc === undefined ? 1 : sc);
  return g;
}

if (typeof module !== 'undefined') module.exports = {
  EXPR, PACKS, MOODS, makeRig, rigSetFace, rigTick, rigBlink, rigImpulse, rigLookAt, rigCloseEyes, Fb, drawFace, renderSettled
};
