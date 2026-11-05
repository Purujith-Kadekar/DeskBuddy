#include "faces.h"
#include "config.h"
#include <math.h>
#include <string.h>

// ============================================================
//  faces.cpp - parametric face engine
//
//  Everything is drawn from horizontal spans (drawHLine) with our
//  own fill routines, so the web mirror (tools/face_renderer.js)
//  produces PIXEL-IDENTICAL output from the same numbers.
// ============================================================

#include "face_table.inc"      // EXPRS[], FACE_NAMES[], PACK_NAMES[], PACK_DESCS[]

const char* faceName(Expression e) { return e < EX_COUNT ? FACE_NAMES[e] : "?"; }
uint8_t faceFlags(Expression e) { return e < EX_COUNT ? EXPRS[e].flags : 0; }
int faceIndexByName(const char* n) {
  for (int i = 0; i < EX_COUNT; i++)
    if (!strcmp(n, FACE_NAMES[i])) return i;
  return -1;
}

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
// snap to 1/64 px so float32 here and float64 in the web mirror always round the same way
static inline float q64(float x) { return lroundf(x * 64.0f) / 64.0f; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// ------------------------------------------------------------
//  Retarget the rig at an expression (pack + mood aware)
// ------------------------------------------------------------
void rigSetFace(FaceRig& r, Expression e, EyePack pack, Mood mood) {
  if (e >= EX_COUNT) e = EX_NEUTRAL;
  if (pack >= PK_COUNT) pack = PK_BOXY;
  if (mood >= MD_COUNT) mood = MD_BALANCED;
  const ExprDef& x = EXPRS[e];
  const MoodDef& m = MOODS[mood];
  const float k = m.express;

  for (int s = 0; s < 2; s++) {
    const EyeT& in = s ? x.R : x.L;
    // mood scales how far the expression departs from neutral
    float w = 24.0f + (in.w - 24.0f) * k;
    float h = 30.0f + (in.h - 30.0f) * k;
    float dy = in.dy * k;
    float lid = clampf(in.lid * k, 0, 0.85f);
    float lidB = clampf(in.lidB * k, 0, 0.7f);
    float slope = clampf(in.slope * k, -1, 1);
    float cheek = clampf(in.cheek * k, 0, 1);
    float dx = in.dx, rr = in.r, shine = in.shine;
    float frac = rr, shineK = 1.0f;

    if (x.glyph == GL_NONE) {                      // glyph eyes (heart, flame ...) keep their own shape
      switch (pack) {
        case PK_BOXY:  frac = fminf(0.85f, rr * 1.1f); shineK = (shine >= 0.8f) ? 0.5f : 0.0f; break;
        case PK_ORB:
          if (h >= 14.0f) {                       // keep closed/line eyes closed
            float sz = (w + h) * 0.5f;
            w += (sz - w) * 0.75f; h += (sz - h) * 0.75f;
          }
          frac = 1.0f; shineK = 1.0f; break;
        case PK_MORPH: frac = 0.22f + 0.78f * (x.morph / 100.0f); shineK = (shine >= 0.8f) ? 0.8f : 0.0f; break;
        case PK_PIXEL: frac = 0; shineK = 0; break;
        case PK_SLEEK: w *= 1.25f; h = fmaxf(3.0f, h * 0.55f); frac = 1.0f; dx += 3.0f; shineK = 0; break;
        case PK_CAT:   w *= 0.8f; h *= 1.12f; frac = 1.0f; shineK = 1.0f; break;
        default: break;
      }
    }
    float* t = r.tgt[s];
    t[P_W] = w; t[P_H] = h; t[P_R] = frac; t[P_DX] = dx; t[P_DY] = dy;
    t[P_LID] = lid; t[P_LIDB] = lidB; t[P_SLOPE] = slope; t[P_CHEEK] = cheek;
    t[P_SHINE] = shine * shineK;
  }
  r.glyphT = x.glyph;
  r.mouthT = x.mouth;
  r.acc = x.acc;
  r.flags = x.flags;
  r.blushT = (float)x.blush;
  r.tempo = m.tempo;
  r.blinkSpeed = m.blinkSpeed;
  r.breatheAmp = m.bounce;
}

// ------------------------------------------------------------
//  Tiny raster toolkit (only drawHLine / drawPixel on the display)
// ------------------------------------------------------------
static U8G2* G;

static inline void px(int x, int y) {
  if (x < 0 || x > 127 || y < 0 || y > 63) return;
  G->drawPixel(x, y);
}
static inline void span(int x0, int x1, int y) {
  if (y < 0 || y > 63 || x0 > x1) return;
  if (x0 < 0) x0 = 0;
  if (x1 > 127) x1 = 127;
  if (x0 > x1) return;
  G->drawHLine(x0, y, x1 - x0 + 1);
}
static void fillRBox(int x, int y, int w, int h, int rad) {
  int mx = (imin(w, h) - 1) / 2;
  if (rad > mx) rad = mx;
  if (rad < 0) rad = 0;
  for (int i = 0; i < h; i++) {
    int d = imin(i, h - 1 - i), inset = 0;
    if (d < rad) {
      float t = rad - d - 0.5f;
      float xo = rad - sqrtf(fmaxf(0.0f, (float)rad * rad - t * t));
      inset = (int)lroundf(xo);
    }
    span(x + inset, x + w - 1 - inset, y + i);
  }
}
static void fillEllipse(int cx, int cy, int rx, int ry) {
  if (rx < 1) rx = 1;
  if (ry < 1) ry = 1;
  for (int yy = -ry; yy <= ry; yy++) {
    float t = yy / (ry + 0.5f);
    float hx = (rx + 0.5f) * sqrtf(fmaxf(0.0f, 1.0f - t * t));
    int ix = (int)floorf(hx);
    span(cx - ix, cx + ix, cy + yy);
  }
}
static void fillTri(int x0, int y0, int x1, int y1, int x2, int y2) {
  int xs[3] = { x0, x1, x2 }, ys[3] = { y0, y1, y2 };
  int ymin = imin(y0, imin(y1, y2)), ymax = imax(y0, imax(y1, y2));
  for (int y = ymin; y <= ymax; y++) {
    float yc = y + 0.5f, lo = 1e9f, hi = -1e9f;
    int n = 0;
    for (int i = 0; i < 3; i++) {
      int j = (i + 1) % 3;
      float ya = (float)ys[i], yb = (float)ys[j];
      if ((ya <= yc && yc < yb) || (yb <= yc && yc < ya)) {
        float xv = xs[i] + (yc - ya) * (xs[j] - xs[i]) / (yb - ya);
        if (xv < lo) lo = xv;
        if (xv > hi) hi = xv;
        n++;
      }
    }
    if (n >= 2) span((int)ceilf(lo - 0.5f), (int)floorf(hi - 0.5f), y);
  }
}
static void line(int x0, int y0, int x1, int y1) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  for (;;) {
    px(x0, y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

// ------------------------------------------------------------
//  One eye
// ------------------------------------------------------------
struct EyeBox { int x0, y0, x1, y1, ex, ey; };

static void drawHeart(int ex, int ey, int iw, int ih, float t, float beat) {
  float b = 1.0f + 0.10f * fmaxf(0.0f, sinf(t * beat));
  float W = iw * b, H = ih * b;
  int rl = (int)lroundf(W * 0.27f);
  if (rl < 2) rl = 2;
  int lx = (int)lroundf(ex - W * 0.235f), rx = (int)lroundf(ex + W * 0.235f), ly = (int)lroundf(ey - H * 0.13f);
  fillEllipse(lx, ly, rl, rl);
  fillEllipse(rx, ly, rl, rl);
  int top = (int)lroundf(ey - H * 0.10f), tip = (int)lroundf(ey + H * 0.50f);
  fillTri(ex - (int)lroundf(W * 0.50f), top, ex + (int)lroundf(W * 0.50f), top, ex, tip);
  if (W >= 14.0f) {                                   // gloss
    G->setDrawColor(0);
    fillEllipse(lx - rl / 3, ly - rl / 3, 1, 1);
    G->setDrawColor(1);
  }
}

static void drawFlame(int ex, int ey, int iw, int ih, float t) {
  float W = (float)iw, H = (float)ih;
  float fl1 = sinf(t * 13.0f) * 0.10f + sinf(t * 21.0f) * 0.05f;
  float fl2 = sinf(t * 17.0f + 1.0f) * 0.08f;
  float hs = 1.0f + 0.08f * sinf(t * 15.0f);
  fillEllipse(ex, ey + (int)lroundf(H * 0.17f), (int)lroundf(W * 0.36f), (int)lroundf(H * 0.33f));
  fillTri((int)lroundf(ex - W * 0.32f), (int)lroundf(ey + H * 0.10f), (int)lroundf(ex + W * 0.32f), (int)lroundf(ey + H * 0.10f),
          (int)lroundf(ex + W * fl1), (int)lroundf(ey - H * 0.52f * hs));
  fillTri((int)lroundf(ex - W * 0.36f), (int)lroundf(ey + H * 0.12f), (int)lroundf(ex - W * 0.05f), (int)lroundf(ey - H * 0.02f),
          (int)lroundf(ex - W * 0.30f + W * fl2), (int)lroundf(ey - H * 0.30f));
  fillTri((int)lroundf(ex + W * 0.05f), (int)lroundf(ey - H * 0.02f), (int)lroundf(ex + W * 0.36f), (int)lroundf(ey + H * 0.12f),
          (int)lroundf(ex + W * 0.30f - W * fl2), (int)lroundf(ey - H * 0.32f));
  G->setDrawColor(0);                                 // hollow inner flame
  fillEllipse(ex, ey + (int)lroundf(H * 0.24f), (int)lroundf(W * 0.20f), (int)lroundf(H * 0.19f));
  fillTri((int)lroundf(ex - W * 0.17f), (int)lroundf(ey + H * 0.22f), (int)lroundf(ex + W * 0.17f), (int)lroundf(ey + H * 0.22f),
          (int)lroundf(ex + W * fl1 * 0.6f), (int)lroundf(ey - H * 0.18f * hs));
  G->setDrawColor(1);
  fillEllipse(ex, ey + (int)lroundf(H * 0.30f), (int)lroundf(W * 0.08f), (int)lroundf(H * 0.09f));   // hot core
}

static void drawChevron(int side, int ex, int ey, int iw, int ih) {      // > <   squeezed shut
  int hw = imax(3, (int)lroundf(iw * 0.40f)), hh = imax(3, (int)lroundf(ih * 0.36f));
  int tipx = side < 0 ? ex + hw : ex - hw, backx = side < 0 ? ex - hw : ex + hw;
  int tk = iw >= 20 ? 3 : 2;
  for (int o = 0; o < tk; o++) {
    line(backx, ey - hh + o, tipx, ey + o);
    line(tipx, ey + o, backx, ey + hh + o);
  }
}

static void drawPaw(int ex, int ey, int iw, int ih) {                    // round eye with two little ears
  fillEllipse(ex, ey + (int)lroundf(ih * 0.08f), (int)lroundf(iw * 0.42f), (int)lroundf(ih * 0.38f));
  int r = imax(1, (int)lroundf(iw * 0.11f));
  fillEllipse(ex - (int)lroundf(iw * 0.25f), ey - (int)lroundf(ih * 0.34f), r, r);
  fillEllipse(ex + (int)lroundf(iw * 0.25f), ey - (int)lroundf(ih * 0.34f), r, r);
  G->setDrawColor(0);
  fillEllipse(ex - (int)lroundf(iw * 0.12f), ey - (int)lroundf(ih * 0.02f), 1, 1);
  G->setDrawColor(1);
}

static void drawArch(int ex, int ey, int iw, int ih) {                   // thin happy ^ eye
  int hw = imax(4, iw / 2 - 1), ah = imax(3, (int)lroundf(ih * 0.38f));
  int th = ih >= 18 ? 3 : 2;
  for (int x = -hw; x <= hw; x++) {
    float u = (float)x / hw;
    int y = (int)lroundf(ey + ah * 0.5f - ah * (1.0f - u * u));
    for (int k = 0; k < th; k++) px(ex + x, y + k);
  }
}

static EyeBox drawEye(int side, const float* p, float w, float h, float exf, float eyf,
                      EyePack pack, uint8_t glyph, float gx, float spin, float tt, float beat) {
  int iw = (int)lroundf(w), ih = (int)lroundf(h);
  if (iw < 2) iw = 2;
  if (ih < 2) ih = 2;
  int ex = (int)lroundf(exf), ey = (int)lroundf(eyf);
  if (pack == PK_PIXEL) {                       // chunky: snap everything to a 2px grid
    iw &= ~1; ih &= ~1; ex &= ~1; ey &= ~1;
    if (iw < 2) iw = 2;
    if (ih < 2) ih = 2;
  }
  int x0 = ex - iw / 2, y0 = ey - ih / 2, x1 = x0 + iw - 1, y1 = y0 + ih - 1;
  EyeBox bx = { x0, y0, x1, y1, ex, ey };
  G->setDrawColor(1);

  if (glyph == GL_X) {
    int hv = imax(3, imin(iw, ih) / 2);
    for (int o = 0; o <= 1; o++) {
      line(ex - hv + o, ey - hv, ex + hv + o, ey + hv);
      line(ex - hv + o, ey + hv, ex + hv + o, ey - hv);
    }
    return bx;
  }
  if (glyph == GL_HEART)   { drawHeart(ex, ey, iw, ih, tt, beat); return bx; }
  if (glyph == GL_FLAME)   { drawFlame(ex, ey, iw, ih, tt);        return bx; }
  if (glyph == GL_CHEVRON) { drawChevron(side, ex, ey, iw, ih);    return bx; }
  if (glyph == GL_PAW)     { drawPaw(ex, ey, iw, ih);              return bx; }
  if (glyph == GL_ARCH)    { drawArch(ex, ey, iw, ih);             return bx; }
  if (glyph == GL_SPIRAL) {
    float R = imin(iw, ih) / 2.0f;
    const int n = 36;
    int lx = 0, ly = 0;
    for (int i = 0; i <= n; i++) {
      float u = i / (float)n;
      float ang = u * 3.0f * 6.2832f + spin;
      float rad = u * R;
      int x = (int)lroundf(ex + cosf(ang) * rad), y = (int)lroundf(ey + sinf(ang) * rad);
      if (i > 0) line(lx, ly, x, y);
      lx = x; ly = y;
    }
    return bx;
  }

  // body
  bool ell = (pack == PK_ORB || pack == PK_CAT || p[P_R] >= 0.97f);
  if (ell) fillEllipse(ex, ey, iw / 2, ih / 2);
  else {
    int rad = (pack == PK_PIXEL) ? 0 : (int)lroundf(p[P_R] * imin(iw, ih) / 2.0f);
    fillRBox(x0, y0, iw, ih, rad);
  }

  // glossy highlight / cat slit
  float sh = p[P_SHINE];
  if (pack == PK_CAT) {
    if (ih >= 10) {
      int pw = 2 + (int)(sh * 4.0f), ph = (int)(ih * 0.85f);
      G->setDrawColor(0);
      fillRBox(ex - pw / 2 + (int)lroundf(gx * 0.5f), ey - ph / 2, pw, ph, pw / 2);
      G->setDrawColor(1);
    }
  } else if (sh > 0.12f && ih >= 12 && iw >= 12) {
    int sr = (int)(1.5f + sh * 3.0f);
    G->setDrawColor(0);
    fillEllipse(ex - iw / 4 + (int)lroundf(gx * 0.6f), ey - ih / 4, sr, sr);
    G->setDrawColor(1);
  }

  G->setDrawColor(0);
  // top lid (flat, tiltable)
  float lid = p[P_LID], sl = p[P_SLOPE];
  if (lid > 0.01f || fabsf(sl) > 0.01f) {
    float yo = y0 + ih * (lid - sl * 0.45f), yi = y0 + ih * (lid + sl * 0.45f);
    float yl = side < 0 ? yo : yi, yr = side < 0 ? yi : yo;     // inner side = towards the nose
    int Yl = (int)lroundf(clampf(yl, (float)(y0 - 1), (float)(y1 + 1)));
    int Yr = (int)lroundf(clampf(yr, (float)(y0 - 1), (float)(y1 + 1)));
    int ya = y0 - 3;
    fillTri(x0 - 2, ya, x1 + 3, ya, x1 + 3, Yr);
    fillTri(x0 - 2, ya, x1 + 3, Yr, x0 - 2, Yl);
  }
  // bottom lid (flat)
  float lb = p[P_LIDB];
  if (lb > 0.01f) {
    int yb = y1 - (int)lroundf(lb * ih) + 1;
    for (int y = yb; y <= y1 + 2; y++) span(x0 - 2, x1 + 2, y);
  }
  // happy cheek: a rounded bump eats the bottom of the eye -> ^ ^
  float ck = p[P_CHEEK];
  if (ck > 0.02f) {
    float ry = ih * 0.65f, rx = iw * 0.75f;
    float ccy = y1 + ry + 2 - ck * ih * 0.95f;
    fillEllipse(ex, (int)lroundf(ccy), (int)lroundf(rx), (int)lroundf(ry));
  }
  G->setDrawColor(1);
  return bx;
}

static void drop(int x, int y) {
  px(x, y - 2);
  span(x - 1, x + 1, y - 1);
  span(x - 1, x + 1, y);
  px(x, y + 1);
}
static void zglyph(int x, int y, int s) {
  span(x, x + s, y);
  line(x + s, y, x, y + s);
  span(x, x + s, y + s);
}

static int insetAt(int i, int h, int rad) {
  int d = imin(i, h - 1 - i);
  if (d >= rad) return 0;
  float t = rad - d - 0.5f;
  return (int)lroundf(rad - sqrtf(fmaxf(0.0f, (float)rad * rad - t * t)));
}
// outline of a rounded box, `th` pixels thick
static void frameRBox(int x, int y, int w, int h, int rad, int th) {
  int mx = (imin(w, h) - 1) / 2;
  if (rad > mx) rad = mx;
  if (rad < 0) rad = 0;
  int irad = rad - th; if (irad < 0) irad = 0;
  int iw = w - 2 * th, ih = h - 2 * th;
  for (int i = 0; i < h; i++) {
    int ins = insetAt(i, h, rad);
    int xl = x + ins, xr = x + w - 1 - ins;
    if (i < th || i >= h - th || iw <= 0 || ih <= 0) { span(xl, xr, y + i); continue; }
    int ii = insetAt(i - th, ih, irad);
    int il = x + th + ii, ir = x + th + iw - 1 - ii;
    span(xl, il - 1, y + i);
    span(ir + 1, xr, y + i);
  }
}

// study glasses: drop in from above and bounce a little when they land
static void drawGlasses(float gl, int cx, int cy, float sc) {
  if (gl < 0.03f) return;
  int lw = (int)lroundf(38 * sc), lh = (int)lroundf(40 * sc), rad = (int)lroundf(10 * sc);
  int th = sc < 0.75f ? 1 : 2;
  float yo = -(1.0f - gl) * 50.0f * sc;
  if (yo > 6.0f * sc) yo = 6.0f * sc;
  int yoff = (int)lroundf(yo);
  int dxl = (int)lroundf(22 * sc);
  int xL = cx - dxl - lw / 2, xR = cx + dxl - lw / 2, y0 = cy + yoff - lh / 2;
  frameRBox(xL, y0, lw, lh, rad, th);
  frameRBox(xR, y0, lw, lh, rad, th);
  int yb = cy + yoff - (int)lroundf(6 * sc);                       // bridge
  for (int k = 0; k < th; k++) span(xL + lw - 1, xR, yb + k);
  int ty = cy + yoff - (int)lroundf(8 * sc), ty2 = cy + yoff - (int)lroundf(13 * sc);
  for (int k = 0; k < th; k++) {                                   // temples
    line(xL, ty + k, 0, ty2 + k);
    line(xR + lw - 1, ty + k, 127, ty2 + k);
  }
}

// ------------------------------------------------------------
//  Mouths (small, only for expressions that need one)
// ------------------------------------------------------------
static void arcMouth(int cx, int my, int hw, int depth, int th, bool smile) {
  if (hw < 1) hw = 1;
  for (int x = -hw; x <= hw; x++) {
    float u = (float)x / hw;
    float off = smile ? depth * (1.0f - u * u) : depth * u * u;
    int y = my + (int)lroundf(off);
    for (int k = 0; k < th; k++) px(cx + x, y + k);
  }
}
static void halfDisc(int cx, int my, int hw, int hh) {                 // flat top, round bottom ("D" on its side)
  if (hh < 1) hh = 1;
  for (int i = 0; i <= hh; i++) {
    float u = (float)i / (hh + 0.5f);
    int hx = (int)floorf((hw + 0.5f) * sqrtf(fmaxf(0.0f, 1.0f - u * u)));
    span(cx - hx, cx + hx, my + i);
  }
}
static const char* const KISS3[7] = { ".###.", "#...#", "....#", "..##.", "....#", "#...#", ".###." };

static void drawMouth(int type, float s, int cx, int my, float t) {
  if (type == MO_NONE || s < 0.25f) return;
  int th = s >= 0.8f ? 2 : 1;
  switch (type) {
    case MO_SMILE:  arcMouth(cx, my, (int)lroundf(6 * s), (int)lroundf(3 * s), th, true); break;
    case MO_TINY:   arcMouth(cx, my + 1, (int)lroundf(3 * s), (int)lroundf(1 * s), 1, true); break;
    case MO_FROWN:  arcMouth(cx, my, (int)lroundf(6 * s), (int)lroundf(3 * s), th, false); break;
    case MO_POUT:   arcMouth(cx, my + 1, (int)lroundf(3 * s), (int)lroundf(2 * s), th, false); break;
    case MO_FLAT:   for (int k = 0; k < th; k++) span(cx - (int)lroundf(5 * s), cx + (int)lroundf(5 * s), my + 1 + k); break;
    case MO_OMEGA:
      arcMouth(cx - (int)lroundf(3 * s), my, (int)lroundf(3 * s), (int)lroundf(2 * s), th, true);
      arcMouth(cx + (int)lroundf(3 * s), my, (int)lroundf(3 * s), (int)lroundf(2 * s), th, true);
      break;
    case MO_GRIN:   halfDisc(cx, my - 1, (int)lroundf(7 * s), (int)lroundf(6 * s)); break;
    case MO_LAUGH:  halfDisc(cx, my - 1, (int)lroundf(9 * s), (int)lroundf(8 * s + 1.2f * sinf(t * 22.0f))); break;
    case MO_WAIL:   fillEllipse(cx, my + (int)lroundf(3 * s), (int)lroundf(7 * s), (int)lroundf((3.0f + 1.4f * sinf(t * 20.0f)) * s)); break;
    case MO_O:      fillEllipse(cx, my + (int)lroundf(2 * s), (int)lroundf(3 * s), (int)lroundf(3 * s)); break;
    case MO_SNORE: {
      float br = 0.5f + 0.5f * sinf(t * 1.7f);
      fillEllipse(cx, my + 2, (int)lroundf(2.5f * s), (int)lroundf((1.5f + 1.3f * br) * s));
      float ph = fmodf(t * 0.5f, 1.0f);                                  // little drool drop
      if (ph < 0.85f) drop(cx + (int)lroundf(4 * s), my + (int)lroundf((3 + ph * 5) * s));
      break;
    }
    case MO_YAWN: {
      float pulse = 0.5f + 0.5f * sinf(t * 1.6f);
      int ry = (int)lroundf((1.5f + 5.0f * pulse) * s);
      fillEllipse(cx, my + ry, (int)lroundf(4 * s), ry);
      break;
    }
    case MO_SMIRK: {
      for (int k = 0; k < th; k++) span(cx - (int)lroundf(5 * s), cx + (int)lroundf(2 * s), my + 1 + k);
      line(cx + (int)lroundf(2 * s), my + 1, cx + (int)lroundf(6 * s), my - 1);
      break;
    }
    case MO_WAVY: {
      int hw = (int)lroundf(7 * s);
      for (int x = -hw; x <= hw; x++) {
        float u = (float)x / (hw > 0 ? hw : 1);
        int y = my + 1 + (int)lroundf(1.4f * s * sinf(u * 9.42f + t * 9.0f));
        for (int k = 0; k < th; k++) px(cx + x, y + k);
      }
      break;
    }
    case MO_TONGUE:
      arcMouth(cx, my, (int)lroundf(6 * s), (int)lroundf(2 * s), th, true);
      fillEllipse(cx + (int)lroundf(2 * s), my + (int)lroundf(5 * s), (int)lroundf(2.6f * s), (int)lroundf(3.2f * s));
      G->setDrawColor(0);
      G->drawPixel(cx + (int)lroundf(2 * s), my + (int)lroundf(4 * s));
      G->drawPixel(cx + (int)lroundf(2 * s), my + (int)lroundf(5 * s));
      G->setDrawColor(1);
      break;
    case MO_KISS: {                                                      // the "3" kiss
      int k = s >= 1.2f ? 2 : 1;
      for (int rr = 0; rr < 7; rr++)
        for (int cc = 0; cc < 5; cc++)
          if (KISS3[rr][cc] == '#')
            for (int dy = 0; dy < k; dy++) span(cx - 2 * k + cc * k, cx - 2 * k + cc * k + k - 1, my - 2 + rr * k + dy);
      break;
    }
    default: break;
  }
}

// ------------------------------------------------------------
//  Whole face
// ------------------------------------------------------------
void drawFace(U8G2& g, const FaceRig& r, EyePack pack, int cy, float sc) {
  G = &g;
  g.setDrawColor(1);
  const int cx = FACE_CX;
  uint8_t fl = r.flags;

  float gl = clampf(r.glasses.x, 0.0f, 1.0f);
  float gk = 1.0f - 0.65f * gl;                        // calmer gaze behind the glasses
  float ln = clampf(r.lean.x, 0.0f, 1.15f);            // head pressing into a petting hand
  float gazeX = r.lookX.x * (pack == PK_SLEEK ? 9.0f : 7.0f) * sc * gk;
  float gazeY = r.lookY.x * 4.5f * sc * gk;
  float sqz = r.squash.x;
  float jig = (fl & F_JIGGLE) ? sinf(r.t * 18.0f) * 1.5f : 0.0f;
  float trem = (fl & F_TREMBLE) ? sinf(r.t * 60.0f) * 1.0f : 0.0f;
  float wob = ((fl & F_WOBBLE) ? sinf(r.t * 8.0f) * 3.0f : 0.0f) + sinf(r.wobblePhase) * r.wobble * 4.0f;
  float offx = (trem + wob) * sc, offy = (jig + r.breatheY + ln * 3.0f) * sc;
  float bl = r.glyph ? 0.0f : r.blink;

  EyeBox box[2];
  for (int s = 0; s < 2; s++) {
    float pq[EYE_NP];
    for (int k = 0; k < EYE_NP; k++) pq[k] = q64(r.cur[s][k]);
    const float* p = pq;
    float w = p[P_W] * sc * (1.0f + (1.0f - sqz) * 0.5f);
    float h = p[P_H] * sc * sqz * (1.0f - bl) * (1.0f - 0.10f * ln);
    if (h < 2.0f) h = 2.0f;
    float ex = cx + (s ? p[P_DX] : -p[P_DX]) * sc + gazeX + offx;
    float ey = cy + p[P_DY] * sc + gazeY + offy;
    box[s] = drawEye(s ? 1 : -1, p, w, h, ex, ey, pack, r.glyph, gazeX, r.spin, r.t, 8.0f + 5.0f * ln);
  }

  // blush (only for the emotions that need it)
  if (r.blush > 0.3f) {
    int n = r.blush > 1.4f ? 3 : 2;
    int by = cy + (int)lroundf(19 * sc);
    for (int sd = -1; sd <= 1; sd += 2) {
      int bx = cx + sd * (int)lroundf(30 * sc);
      for (int i = 0; i < n; i++) {
        int x = bx - n * 2 - 1 + i * 5;
        line(x, by + 3, x + 3, by);
      }
    }
  }

  // mouth: sits just under whatever the eyes currently look like
  {
    static const float GBF[8] = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.38f, 0.46f, 0.26f };
    float hb = q64(fmaxf(r.cur[0][P_H], r.cur[1][P_H]));
    float dyb = q64(fmaxf(r.cur[0][P_DY], r.cur[1][P_DY]));
    float bf = GBF[r.glyph < 8 ? r.glyph : 0];
    if (r.glyph == GL_NONE) bf -= 0.32f * q64(fmaxf(r.cur[0][P_CHEEK], r.cur[1][P_CHEEK]));
    int my = cy + (int)lroundf((dyb + hb * bf + 7.0f) * sc + offy);
    if (gl > 0.5f) { int mn = cy + (int)lroundf(24.0f * sc); if (my < mn) my = mn; }
    int mx = cy + (int)lroundf(27.0f * sc);
    if (my > mx) my = mx;
    if (sc >= 0.45f && my < 60) drawMouth(r.mouth, q64(r.mouthS.x * sc), cx, my, r.t);
  }

  // accessories that carry the emotion
  if (r.acc == AC_HEART) {                              // one tiny heart that pulses in place
    int hs = (int)lroundf(10.0f * sc);
    drawHeart(cx + (int)lroundf(36.0f * sc), cy - (int)lroundf(19.0f * sc), hs, hs - 1, r.t, 7.0f);
  } else if (r.acc == AC_TEAR) {
    for (int s = 0; s < 2; s++) {
      float ph = fmodf(r.t * 0.75f + s * 0.4f, 1.0f);
      if (ph < 0.9f) {
        int iw = box[s].x1 - box[s].x0 + 1;
        int tx = box[s].ex + (s ? 1 : -1) * (iw / 2 - 4);
        drop(tx, box[s].y1 - 2 + (int)(ph * 18.0f * sc));
      }
    }
  } else if (r.acc == AC_SWEAT) {
    float ph = fmodf(r.t * 0.9f, 1.0f);
    drop(box[1].x1 + 4, box[1].y0 - 2 + (int)(ph * 14.0f * sc));
  } else if (r.acc == AC_ZZZ) {
    for (int k = 0; k < 2; k++) {
      float ph = fmodf(r.t * 0.4f + k * 0.5f, 1.0f);
      if (ph < 0.85f)
        zglyph(cx + (int)lroundf(22 * sc) + (int)(ph * 12.0f * sc), cy - (int)lroundf(6 * sc) - (int)(ph * 18.0f * sc), 3 + k);
    }
  }

  drawGlasses(r.glasses.x, cx, cy, sc);
}

void gfxRBox(U8G2& g, int x, int y, int w, int h, int rad, uint8_t color) {
  G = &g;
  g.setDrawColor(color);
  fillRBox(x, y, w, h, rad);
  g.setDrawColor(1);
}
void gfxHLine(U8G2& g, int x, int y, int w, uint8_t color) {
  G = &g;
  g.setDrawColor(color);
  span(x, x + w - 1, y);
  g.setDrawColor(1);
}
void gfxEllipse(U8G2& g, int cx, int cy, int rx, int ry, uint8_t color) {
  G = &g; g.setDrawColor(color); fillEllipse(cx, cy, rx, ry); g.setDrawColor(1);
}
void gfxLine(U8G2& g, int x0, int y0, int x1, int y1, uint8_t color) {
  G = &g; g.setDrawColor(color); line(x0, y0, x1, y1); g.setDrawColor(1);
}
void gfxTri(U8G2& g, int x0, int y0, int x1, int y1, int x2, int y2, uint8_t color) {
  G = &g; g.setDrawColor(color); fillTri(x0, y0, x1, y1, x2, y2); g.setDrawColor(1);
}
void gfxPixel(U8G2& g, int x, int y, uint8_t color) {
  G = &g; g.setDrawColor(color); px(x, y); g.setDrawColor(1);
}
