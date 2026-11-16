#!/usr/bin/env python3
"""
gen_faces.py - single source of truth for Desk Buddy faces, eye styles and moods.

Edit the EXPRESSIONS / PACKS / MOODS tables below, then run:

    python3 tools/gen_faces.py

It (re)writes:
    include/face_enum.inc  C++ enums (expressions, glyph eyes, accessories, mouths)
    src/face_table.inc     C++ expression + pack tables
    src/mood_table.inc     C++ personality table
    src/emo_table.inc      C++ table that maps each expression to its buzzer voice
    src/webface.cpp        JS renderer + tables served to the web UI live mirror

The OLED and the web mirror are drawn from the SAME numbers by the SAME algorithm.

Eye parameters (pixels on the 128x64 screen):
    w,h     eye width / height          r      roundness 0..1
    dx      distance of the eye from centre   dy   vertical offset
    lid     top lid coverage 0..1       lidB   bottom lid coverage
    slope   lid tilt (+angry -sad)      cheek  happy-arch cut 0..1
    shine   glossy highlight size 0..1
Glyph eyes (heart, flame, chevron ...) ignore the eye-style pack.
"""
import os, json

F_JIGGLE, F_TREMBLE, F_WOBBLE, F_LOOK_SIDE, F_LOOK_UP, F_AVERT = 1, 2, 4, 8, 16, 32

GLYPHS = ["none", "spiral", "x", "heart", "flame", "chevron", "paw", "arch"]
ACCS   = ["none", "tear", "sweat", "zzz", "heart"]
MOUTHS = ["none", "smile", "tiny", "grin", "laugh", "frown", "pout", "flat", "omega",
          "kiss", "wail", "o", "snore", "yawn", "smirk", "wavy", "tongue"]
G = {n: i for i, n in enumerate(GLYPHS)}
A = {n: i for i, n in enumerate(ACCS)}
MO = {n: i for i, n in enumerate(MOUTHS)}

KEYS = ["w", "h", "r", "dx", "dy", "lid", "lidB", "slope", "cheek", "shine"]
NEUTRAL = dict(w=24, h=30, r=.45, dx=22, dy=0, lid=0, lidB=0, slope=0, cheek=0, shine=.5)


def E(**kw):
    d = dict(NEUTRAL)
    d.update(kw)
    return d


HAPPY_ARCH = dict(w=28, h=28, r=.5, dy=6, cheek=.72, shine=0)

# name, L, R(None = mirror), glyph, blush(0-2), accessory, flags, morph(0..100), mouth
EXPRESSIONS = [
    # ---- the original set ----
    ("neutral",   E(),                                                       None, "none", 0, "none", 0, 0, "smile"),
    ("happy",     E(**HAPPY_ARCH),                                           None, "none", 0, "none", 0, 70, "grin"),
    ("laugh",     E(w=30, h=24, r=.5, dx=23, dy=5, cheek=.85, shine=0),      None, "none", 1, "none", F_JIGGLE, 80, "laugh"),
    ("love",      E(w=30, h=28, dy=1),                                       None, "heart", 2, "none", 0, 100, "smile"),
    ("wink",      E(),                                   E(**HAPPY_ARCH),          "none", 1, "none", 0, 30, "smirk"),
    ("shy",       E(w=22, h=24, r=.5, dy=3, lid=.2, shine=0),                None, "none", 2, "none", F_AVERT, 60, "tiny"),
    ("surprised", E(w=26, h=36, r=.5, dy=-3, shine=.8),                      None, "none", 0, "none", 0, 100, "o"),
    ("angry",     E(w=26, h=26, r=.3, dx=21, dy=1, lid=.28, slope=.75, shine=0), None, "none", 0, "none", 0, 0, "frown"),
    ("sad",       E(h=28, dy=2, lid=.22, slope=-.8, shine=.7),               None, "none", 0, "none", 0, 20, "frown"),
    ("cry",       E(h=24, r=.5, dy=3, lid=.28, slope=-.75, shine=0),         None, "none", 1, "tear", F_TREMBLE, 60, "wail"),
    ("sleepy",    E(h=28, dy=2, lid=.5, shine=.3),                           None, "none", 0, "none", 0, 10, "flat"),
    ("sleep",     E(w=22, h=4, r=1, dy=3, shine=0),                          None, "none", 0, "zzz", 0, 50, "snore"),
    ("focus",     E(w=28, h=22, r=.25, dy=1, lid=.14, lidB=.1, slope=.3, shine=0), None, "none", 0, "none", 0, 0, "flat"),
    ("chill",     E(w=26, h=26, r=.5, dy=2, lid=.4, cheek=.35, shine=0),     None, "none", 0, "none", 0, 30, "smile"),
    ("dizzy",     E(w=30, h=30, r=.5, shine=0),                              None, "spiral", 0, "none", F_WOBBLE, 100, "wavy"),
    ("bored",     E(h=24, r=.35, dy=3, lid=.5, shine=.2),                    None, "none", 0, "none", 0, 0, "flat"),
    ("confused",  E(),                      E(w=20, h=20, r=.4, dy=-3, lid=.2, slope=.5, shine=0), "none", 0, "none", 0, 30, "wavy"),
    ("panic",     E(w=30, h=34, r=.5, shine=0),                              None, "none", 0, "sweat", F_TREMBLE, 100, "wail"),
    ("smug",      E(w=26, h=22, r=.4, dy=2, lid=.38, slope=.22, cheek=.3, shine=0), None, "none", 0, "none", 0, 10, "smirk"),
    ("sus",       E(w=28, h=18, r=.3, dy=2, lid=.3, lidB=.28, shine=0),      None, "none", 0, "none", F_LOOK_SIDE, 0, "flat"),
    ("party",     E(w=28, h=28, r=.5, dy=5, cheek=.72, shine=0),             None, "none", 1, "none", F_JIGGLE, 100, "laugh"),
    ("thinking",  E(dy=-1),                 E(w=22, h=24, r=.45, dy=-4, lid=.15, slope=.3, shine=.4), "none", 0, "none", F_LOOK_UP, 20, "pout"),
    ("wow",       E(w=30, h=32, r=.55, dy=-2, shine=1.0),                    None, "none", 0, "none", 0, 100, "o"),
    ("dead",      E(w=24, h=24, r=.2, shine=0),                              None, "x", 0, "none", 0, 0, "tongue"),
    # ---- from your reference sheet ----
    ("serious",   E(w=26, h=22, r=.3, dy=1, lid=.34, slope=.55, shine=0),    None, "none", 0, "none", 0, 0, "smile"),
    ("bawl",      E(w=24, h=26),                                             None, "chevron", 1, "tear", F_TREMBLE, 100, "wail"),
    ("cute",      E(w=26, h=28, dy=1),                                       None, "paw", 1, "none", 0, 100, "flat"),
    ("kiss",      E(w=24, h=26, r=.5, dy=1, lid=.22, shine=0),               None, "none", 2, "none", F_AVERT, 60, "kiss"),
    ("minilove",  E(w=26, h=26, dy=2),                                       None, "arch", 1, "heart", 0, 60, "omega"),
    ("animelove", E(w=24, h=22, dy=1),                                       None, "heart", 2, "none", 0, 100, "omega"),
    ("fire",      E(w=32, h=38, dy=-1),                                      None, "flame", 0, "none", 0, 100, "tiny"),
    # ---- extra personality ----
    ("blep",      E(w=26, h=24, r=.5, dy=4, cheek=.55, shine=0),             None, "none", 1, "none", 0, 60, "tongue"),
    ("yawn",      E(w=26, h=16, r=.5, dy=4, lid=.25, cheek=.5, shine=0),     None, "none", 0, "none", 0, 40, "yawn"),
    ("bliss",     E(w=28, h=24, r=.5, dy=6, cheek=.8, shine=0),              None, "none", 2, "none", 0, 70, "smile"),
    ("giggle",    E(w=26, h=26, r=.5, dy=4, cheek=.6, shine=0),              None, "none", 1, "none", F_JIGGLE, 80, "grin"),
    ("pout",      E(w=24, h=24, r=.4, dy=2, lid=.26, slope=.4, shine=.3),    None, "none", 2, "none", 0, 30, "pout"),
    ("nervous",   E(w=22, h=30, r=.5, shine=.6),                             None, "none", 0, "sweat", F_TREMBLE | F_LOOK_SIDE, 70, "wavy"),
    ("dreamy",    E(w=26, h=24, r=.5, dy=-1, lid=.28, cheek=.3, shine=.5),   None, "none", 1, "none", F_LOOK_UP, 60, "smile"),
    ("mischief",  E(w=26, h=20, r=.3, dy=2, lid=.3, slope=.6, lidB=.1, shine=0), None, "none", 0, "none", 0, 10, "grin"),
    ("xd",        E(w=24, h=22),                                             None, "chevron", 0, "none", F_JIGGLE, 100, "laugh"),
    ("whistle",   E(w=22, h=22, r=.5, dy=-1, lid=.15, shine=.5),             None, "none", 0, "none", F_LOOK_UP, 70, "o"),
]
# fix the symbolic fields above (strings -> ids)
def _fix(row):
    n, L, R, gl, bl, ac, fl, mo, mth = row
    return (n, L, R, G[gl], bl, A[ac], fl, mo, MO[mth])
EXPRESSIONS = [_fix(r) for r in EXPRESSIONS]
NAMES = [e[0] for e in EXPRESSIONS]

PACKS = [
    ("boxy",  "Rounded rectangles"),
    ("orb",   "Soft round orbs"),
    ("morph", "Rect that turns round"),
    ("pixel", "Chunky retro pixels"),
    ("sleek", "Thin robot visor"),
    ("cat",   "Cat eyes, slit pupils"),
]

def S(*steps):
    return list(steps)

# petPhase = face while being petted (0-1.8 s, 1.8-3.6 s, longer); afterglow = face right after
MOODS = [
    dict(name="balanced", desc="Easy-going buddy, a bit of everything",
         blink=(2500, 6000), look=(1800, 3600), chg=(7000, 16000), scene=(14000, 28000),
         lookAmp=1.0, express=1.0, tempo=1.0, bounce=1.0, blinkSpeed=1.0, sfx=1, home="neutral",
         pool=["neutral", "neutral", "neutral", "happy", "wink", "chill", "smug", "thinking", "sus", "cute",
               "serious", "whistle", "dreamy", "blep", "minilove", "giggle"],
         pet=["happy", "cute", "giggle", "blep"],
         petPhase=["bliss", "bliss", "love"], afterglow="happy",
         scenes=[S(("wow", 1000), ("cute", 1300), ("minilove", 1800)),
                 S(("thinking", 1500), ("whistle", 1800), ("happy", 1200))]),
    dict(name="dramatic", desc="Everything is a TRAGEDY. Or a triumph.",
         blink=(4000, 9000), look=(1200, 2600), chg=(3500, 8000), scene=(18000, 35000),
         lookAmp=1.2, express=1.3, tempo=1.15, bounce=1.4, blinkSpeed=1.0, sfx=1, home="neutral",
         pool=["surprised", "sad", "cry", "bawl", "angry", "wow", "panic", "dead", "love", "animelove",
               "dizzy", "fire", "mischief", "nervous", "xd", "smug"],
         pet=["wow", "bawl", "animelove", "fire"],
         petPhase=["wow", "animelove", "love"], afterglow="bawl",
         scenes=[S(("wow", 1100), ("sad", 1500), ("cry", 1800), ("bawl", 1800), ("angry", 1400), ("fire", 1500), ("smug", 1300)),
                 S(("panic", 1000), ("dizzy", 1200), ("dead", 1700), ("sleepy", 1100), ("neutral", 900))]),
    dict(name="silent", desc="Calm and quiet. Slow blinks, no sound effects",
         blink=(4500, 9500), look=(3500, 7000), chg=(14000, 30000), scene=(0, 0),
         lookAmp=.45, express=.8, tempo=.75, bounce=.5, blinkSpeed=.6, sfx=0, home="neutral",
         pool=["neutral", "neutral", "neutral", "cute", "bored", "sleepy", "chill", "serious", "dreamy", "yawn", "whistle", "minilove"],
         pet=["happy", "cute", "bliss"],
         petPhase=["bliss", "bliss", "minilove"], afterglow="neutral",
         scenes=[]),
    dict(name="active", desc="Hyper! Darting eyes, bouncy, always up to something",
         blink=(1500, 3500), look=(600, 1600), chg=(2500, 6000), scene=(12000, 24000),
         lookAmp=1.3, express=1.1, tempo=1.45, bounce=1.7, blinkSpeed=1.4, sfx=1, home="happy",
         pool=["happy", "laugh", "party", "wink", "wow", "surprised", "giggle", "xd", "blep", "fire",
               "mischief", "whistle", "confused", "thinking", "minilove", "cute"],
         pet=["giggle", "xd", "party", "laugh"],
         petPhase=["giggle", "xd", "party"], afterglow="laugh",
         scenes=[S(("wow", 900), ("laugh", 1500), ("party", 1800)),
                 S(("mischief", 1300), ("blep", 1400), ("xd", 1500))]),
    dict(name="romantic", desc="Dreamy, blushy, a little flirty",
         blink=(3000, 7000), look=(2500, 5000), chg=(6000, 12000), scene=(15000, 30000),
         lookAmp=.6, express=1.05, tempo=.9, bounce=.8, blinkSpeed=.8, sfx=1, home="dreamy",
         pool=["love", "animelove", "minilove", "kiss", "kiss", "shy", "shy", "wink", "dreamy", "dreamy",
               "bliss", "cute", "happy", "chill", "sleepy", "blep"],
         pet=["love", "shy", "kiss", "minilove"],
         petPhase=["shy", "bliss", "animelove"], afterglow="kiss",
         scenes=[S(("shy", 1500), ("love", 2200), ("kiss", 1500), ("wink", 1200))]),
    dict(name="grumpy", desc="Not a morning buddy. Side-eye specialist",
         blink=(3000, 8000), look=(2000, 4500), chg=(6000, 14000), scene=(16000, 30000),
         lookAmp=.8, express=1.1, tempo=.95, bounce=.6, blinkSpeed=1.0, sfx=1, home="bored",
         pool=["angry", "bored", "bored", "sus", "sus", "serious", "smug", "pout", "focus", "fire", "yawn", "sad"],
         pet=["angry", "pout", "smug"],
         petPhase=["pout", "bliss", "minilove"], afterglow="smug",
         scenes=[S(("angry", 1400), ("fire", 1500), ("pout", 1500), ("smug", 1500))]),
    dict(name="sleepy", desc="Lazy and drowsy, naps a lot",
         blink=(2000, 5000), look=(3000, 6000), chg=(9000, 20000), scene=(20000, 40000),
         lookAmp=.5, express=.9, tempo=.7, bounce=.5, blinkSpeed=.7, sfx=1, home="sleepy",
         pool=["sleepy", "sleepy", "sleepy", "sleep", "sleep", "yawn", "yawn", "bored", "neutral", "bliss",
               "dreamy", "happy", "chill"],
         pet=["sleepy", "bliss", "happy"],
         petPhase=["bliss", "sleepy", "sleep"], afterglow="yawn",
         scenes=[S(("yawn", 2000), ("sleepy", 1600), ("sleep", 3000), ("surprised", 900), ("neutral", 1000))]),
]

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def f(v):
    s = ("%.3f" % v).rstrip("0").rstrip(".")
    if s in ("", "-0"):
        s = "0"
    if "." not in s:
        s += ".0"
    return s + "f"


def eye_c(d):
    return "{" + ",".join(f(d[k]) for k in KEYS) + "}"


def ex(name):
    return "EX_" + name.upper()


def gen_enum():
    out = ["// AUTO-GENERATED by tools/gen_faces.py - do not edit by hand\n"]
    out.append("enum Expression : uint8_t {\n  " + ",\n  ".join(
        (ex(n) + (" = 0" if i == 0 else "")) for i, n in enumerate(NAMES)) + ",\n  EX_COUNT\n};\n")
    out.append("enum : uint8_t { " + ", ".join(("GL_" + g.upper()) + (" = 0" if i == 0 else "") for i, g in enumerate(GLYPHS)) + " };\n")
    out.append("enum : uint8_t { " + ", ".join(("AC_" + a.upper()) + (" = 0" if i == 0 else "") for i, a in enumerate(ACCS)) + " };\n")
    out.append("enum : uint8_t { " + ", ".join(("MO_" + m.upper()) + (" = 0" if i == 0 else "") for i, m in enumerate(MOUTHS)) + ", MO_COUNT };\n")
    open(os.path.join(ROOT, "include", "face_enum.inc"), "w").write("".join(out))


def gen_cpp_faces():
    out = ["// AUTO-GENERATED by tools/gen_faces.py - do not edit by hand\n"]
    out.append("const ExprDef EXPRS[EX_COUNT] = {\n")
    for (n, L, R, gl, bl, ac, fl, mo, mth) in EXPRESSIONS:
        out.append("  /*%-10s*/ { %s, %s, %d, %d, %d, %d, %d, %d },\n" %
                   (n, eye_c(L), eye_c(R or L), gl, bl, ac, fl, mo, mth))
    out.append("};\n\nconst char* const FACE_NAMES[EX_COUNT] = {\n  ")
    out.append(", ".join('"%s"' % n for n in NAMES))
    out.append("\n};\n\nconst char* const PACK_NAMES[PK_COUNT] = {\n  ")
    out.append(", ".join('"%s"' % p[0] for p in PACKS))
    out.append("\n};\nconst char* const PACK_DESCS[PK_COUNT] = {\n  ")
    out.append(", ".join('"%s"' % p[1] for p in PACKS))
    out.append("\n};\n")
    open(os.path.join(ROOT, "src", "face_table.inc"), "w").write("".join(out))


def gen_cpp_moods():
    out = ["// AUTO-GENERATED by tools/gen_faces.py - do not edit by hand\n"]
    for mi, m in enumerate(MOODS):
        for si, sc in enumerate(m["scenes"]):
            out.append("static const Scene SC_%d_%d[] = { %s };\n" % (
                mi, si, ", ".join("{%s,%d}" % (ex(e), ms) for e, ms in sc)))
    out.append("\nconst MoodDef MOODS[MD_COUNT] = {\n")
    for mi, m in enumerate(MOODS):
        assert len(m["pool"]) <= 16 and len(m["pet"]) <= 4 and len(m["scenes"]) <= 2
        pool = [ex(p) for p in m["pool"]]
        pet = [ex(p) for p in m["pet"]]
        pp = [ex(p) for p in m["petPhase"]]
        sc = m["scenes"]
        sc_ptr = ["SC_%d_%d" % (mi, i) for i in range(len(sc))] + ["nullptr"] * (2 - len(sc))
        sc_len = [len(s) for s in sc] + [0] * (2 - len(sc))
        out.append(
            '  { "%s", "%s", %d,%d, %d,%d, %d,%d, %d,%d, %s,%s,%s,%s,%s, %d, %s,\n'
            '    %d, {%s}, %d, {%s}, {%s}, %s, %d, {%s}, {%s} },\n' % (
                m["name"], m["desc"],
                m["blink"][0], m["blink"][1], m["look"][0], m["look"][1],
                m["chg"][0], m["chg"][1], m["scene"][0], m["scene"][1],
                f(m["lookAmp"]), f(m["express"]), f(m["tempo"]), f(m["bounce"]), f(m["blinkSpeed"]),
                m["sfx"], ex(m["home"]),
                len(pool), ",".join(pool), len(pet), ",".join(pet), ",".join(pp), ex(m["afterglow"]),
                len(sc), ",".join(sc_ptr), ",".join(str(x) for x in sc_len)))
    out.append("};\n")
    open(os.path.join(ROOT, "src", "mood_table.inc"), "w").write("".join(out))


def gen_emo_table():
    out = ["// AUTO-GENERATED by tools/gen_faces.py - one buzzer voice per expression (defined in audio.cpp)\n"]
    out.append("static const Seq EMO_TABLE[EX_COUNT] = {\n")
    for n in NAMES:
        out.append("  {E_%s, LEN(E_%s)},\n" % (n.upper(), n.upper()))
    out.append("};\n")
    open(os.path.join(ROOT, "src", "emo_table.inc"), "w").write("".join(out))


def gen_js():
    exr = []
    for (n, L, R, gl, bl, ac, fl, mo, mth) in EXPRESSIONS:
        exr.append([n, [L[k] for k in KEYS], [(R or L)[k] for k in KEYS], gl, bl, ac, fl, mo, mth])
    mo = []
    for m in MOODS:
        mo.append(dict(n=m["name"], d=m["desc"], h=NAMES.index(m["home"]), la=m["lookAmp"], ex=m["express"],
                       tp=m["tempo"], bo=m["bounce"], bs=m["blinkSpeed"], bl=list(m["blink"])))
    consts = "".join("const GL_%s=%d;" % (g.upper(), i) for i, g in enumerate(GLYPHS)) + \
             "".join("const AC_%s=%d;" % (a.upper(), i) for i, a in enumerate(ACCS)) + \
             "".join("const MO_%s=%d;" % (m.upper(), i) for i, m in enumerate(MOUTHS)) + "\n"
    js_tables = consts + ("const EXPR=%s;\nconst PACKS=%s;\nconst MOODS=%s;\n" % (
        json.dumps(exr, separators=(",", ":")),
        json.dumps([[p[0], p[1]] for p in PACKS], separators=(",", ":")),
        json.dumps(mo, separators=(",", ":"))))
    renderer = open(os.path.join(ROOT, "tools", "face_renderer.js")).read()
    body = "// AUTO-GENERATED by tools/gen_faces.py - do not edit by hand\n" \
           "// (JS renderer = tools/face_renderer.js)\n" \
           '#include "webface.h"\n\n' \
           "const char FACE_JS[] PROGMEM = R\"JSRAW(\n" + js_tables + renderer + ")JSRAW\";\n"
    open(os.path.join(ROOT, "src", "webface.cpp"), "w").write(body)
    open("/tmp/face_all.js", "w").write(js_tables + renderer)


if __name__ == "__main__":
    gen_enum()
    gen_cpp_faces()
    gen_cpp_moods()
    gen_emo_table()
    gen_js()
    print("generated: %d faces, %d packs, %d moods, %d mouth types" % (len(EXPRESSIONS), len(PACKS), len(MOODS), len(MOUTHS) - 1))
