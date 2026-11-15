#!/usr/bin/env python3
"""Generate the Fallen panels (res/*.svg), component SVGs (res/components/*.svg) and the
widget placement header (src/generated/Layout.hpp) from one table per module.

Every control is placed from the same coordinates in both outputs, every control gets a
printed label (its own, or its grid's row and column headers), and the script fails if
anything overlaps or leaves the panel.

    python3 scripts/make_panels.py            (needs: pip install fonttools; Inter font)

Text is converted to paths here, because Rack does not render SVG text.
"""
import math
import os
import sys

from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FONT_DIR = "/usr/share/fonts/opentype/inter/"
FONT = FONT_DIR + "Inter-Medium.otf"
FONT_BOLD = FONT_DIR + "Inter-Bold.otf"

# ---------------------------------------------------------------- look ------
GROUND = "#f1f1ee"
INK = "#111111"
ACCENT = "#e04a1f"
PLATE = "#111111"
PLATE_INK = "#f1f1ee"
H = 128.5
HP = 5.08
LABEL_CAP = 1.8
SMALL_CAP = 1.5
TITLE_CAP = 4.0
DESC_CAP = 1.7

KNOBS = {  # size: (body radius, scale radius, check box half-size, C++ class)
    "L": (4.5, 5.4, 5.8, "FallenKnob"),
    "S": (3.2, 4.0, 4.3, "FallenKnobSmall"),
    "X": (2.6, 3.2, 3.5, "FallenKnobTiny"),
}
JACK_R = 4.0
JACK_BOX = 4.2
LED_R = 1.0
BTN_R = 2.8
SW_W, SW_H = 4.6, 8.2


# ---------------------------------------------------------------- text ------
class Face:
    def __init__(self, path):
        self.font = TTFont(path)
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.cap = getattr(self.font["OS/2"], "sCapHeight", 0) or 0.73 * self.font["head"].unitsPerEm
        self.hmtx = self.font["hmtx"]

    def width(self, s, cap):
        scale = cap / self.cap
        w = 0
        for ch in s:
            g = self.cmap.get(ord(ch))
            if g is None:
                raise SystemExit(f"font has no glyph for {ch!r}")
            w += self.hmtx[g][0]
        return w * scale

    def path(self, s, x, y, cap, anchor="middle"):
        scale = cap / self.cap
        w = self.width(s, cap)
        if anchor == "middle":
            x -= w / 2
        elif anchor == "end":
            x -= w
        d = []
        pen_x = x
        for ch in s:
            g = self.cmap[ord(ch)]
            sp = SVGPathPen(self.glyphs)
            tp = TransformPen(sp, (scale, 0, 0, -scale, pen_x, y))
            self.glyphs[g].draw(tp)
            d.append(sp.getCommands())
            pen_x += self.hmtx[g][0] * scale
        return " ".join(p for p in d if p)


REG = Face(FONT)
BOLD = Face(FONT_BOLD)


def text_width(s, cap):
    return REG.width(s, cap)


def text_path(s, x, y, cap, anchor="middle"):
    return REG.path(s, x, y, cap, anchor)


# ---------------------------------------------------------------- model -----
class Box:
    def __init__(self, x0, y0, x1, y1, what, owner=None):
        self.x0, self.y0, self.x1, self.y1, self.what, self.owner = x0, y0, x1, y1, what, owner

    def overlaps(self, o, pad=0.2):
        return not (self.x1 + pad <= o.x0 or o.x1 + pad <= self.x0 or self.y1 + pad <= o.y0 or o.y1 + pad <= self.y0)


class Panel:
    def __init__(self, slug, desc, hp):
        self.slug, self.desc, self.hp = slug, desc, hp
        self.w = hp * HP
        self.items = []      # (kind, id, label, x, y, extra)
        self.displays = []   # (id, x0, y0, x1, y1)
        self.texts = []      # (text, x, y, cap, anchor)
        self.lines = []
        self.grid_ok = set()  # ids labelled by row/column headers

    def knob(self, pid, label, x, y, size="L", bipolar=False, grid=False):
        assert pid.endswith("_PARAM"), pid
        self.items.append(("knob", pid, None if grid else label, x, y, {"size": size, "bipolar": bipolar, "grid": grid, "tip": label}))

    def jack(self, pid, label, x, y, grid=False):
        assert pid.endswith("_INPUT") or pid.endswith("_OUTPUT"), pid
        self.items.append(("jack", pid, None if grid else label, x, y, {"out": pid.endswith("_OUTPUT"), "grid": grid, "tip": label}))

    def switch(self, pid, positions, x, y, label=None):
        self.items.append(("switch", pid, label, x, y, {"positions": positions}))

    def button(self, pid, light, label, x, y, latch=False, grid=False):
        self.items.append(("button", pid, None if grid else label, x, y, {"light": light, "latch": latch, "grid": grid, "tip": label}))

    def led(self, pid, label, x, y, grid=False):
        self.items.append(("led", pid, None if grid else label, x, y, {"grid": grid}))

    def display(self, did, x0, y0, x1, y1):
        self.displays.append((did, x0, y0, x1, y1))

    def text(self, t, x, y, cap=SMALL_CAP, anchor="middle"):
        self.texts.append((t, x, y, cap, anchor))

    def line(self, x0, y0, x1, y1):
        self.lines.append((x0, y0, x1, y1))

    # labels -------------------------------------------------------------
    def label_lines(self, item):
        kind, pid, label, x, y, ex = item
        out = []
        if not label:
            return out
        lines = label.split("\n")
        if kind == "knob":
            r = KNOBS[ex["size"]][1]
            base = y - r - 1.0
            for i, ln in enumerate(reversed(lines)):
                out.append((ln, x, base - i * (LABEL_CAP + 0.7), LABEL_CAP, "middle"))
        elif kind == "jack":
            base = y + JACK_R + 0.9 + LABEL_CAP
            for i, ln in enumerate(lines):
                out.append((ln, x, base + i * (LABEL_CAP + 0.7), LABEL_CAP, "middle"))
        elif kind == "led":
            out.append((lines[0], x, y + LED_R + 0.8 + SMALL_CAP, SMALL_CAP, "middle"))
        elif kind == "button":
            out.append((lines[0], x, y - BTN_R - 1.0, LABEL_CAP, "middle"))
        elif kind == "switch":
            base = y - SW_H / 2 - 1.0
            for i, ln in enumerate(reversed(lines)):
                out.append((ln, x, base - i * (LABEL_CAP + 0.7), LABEL_CAP, "middle"))
        return out

    def extra_labels(self, item):
        kind, pid, label, x, y, ex = item
        out = []
        if kind == "switch":
            pos = ex["positions"]
            ys = [y - 2.7, y + 2.7 + SMALL_CAP] if len(pos) == 2 else [y - 2.7, y + SMALL_CAP / 2, y + 2.7 + SMALL_CAP]
            out += [(p_, x + SW_W / 2 + 0.6, yy, SMALL_CAP, "start") for p_, yy in zip(pos, ys)]
        return out

    def all_labels(self):
        out = []
        for it in self.items:
            out += [(t, it) for t in self.label_lines(it)]
            out += [(t, it) for t in self.extra_labels(it)]
        return out

    def boxes(self):
        b = []
        for it in self.items:
            kind, pid, label, x, y, ex = it
            r = {"jack": JACK_BOX, "led": LED_R + 0.2, "button": BTN_R + 0.2}.get(kind)
            if kind == "knob":
                r = KNOBS[ex["size"]][2]
            if r:
                b.append(Box(x - r, y - r, x + r, y + r, pid, pid))
            else:
                b.append(Box(x - SW_W / 2, y - SW_H / 2, x + SW_W / 2, y + SW_H / 2, pid, pid))
        for (t, x, base, cap, anchor), it in self.all_labels():
            w = text_width(t, cap)
            x0 = x - w / 2 if anchor == "middle" else x if anchor == "start" else x - w
            b.append(Box(x0, base - cap, x0 + w, base, f"label '{t}' of {it[1]}", it[1] if it[0] == "switch" else None))
        for (t, x, base, cap, anchor) in self.texts:
            w = text_width(t, cap)
            x0 = x - w / 2 if anchor == "middle" else x if anchor == "start" else x - w
            b.append(Box(x0, base - cap, x0 + w, base, f"text '{t}'"))
        for (did, x0, y0, x1, y1) in self.displays:
            b.append(Box(x0, y0, x1, y1, f"display {did}"))
        return b

    def check(self):
        errs = []
        bx = self.boxes()
        for i in range(len(bx)):
            a = bx[i]
            if a.x0 < 1.2 or a.x1 > self.w - 1.2 or a.y0 < 15.5 or a.y1 > 121.5:
                errs.append(f"{a.what} leaves the panel area ({a.x0:.1f},{a.y0:.1f})-({a.x1:.1f},{a.y1:.1f})")
            for j in range(i + 1, len(bx)):
                if a.owner and a.owner == bx[j].owner:
                    continue
                if a.overlaps(bx[j]):
                    errs.append(f"{a.what} overlaps {bx[j].what}")
        for it in self.items:
            labelled = bool(it[2]) or it[0] == "switch" or it[5].get("grid")
            if not labelled:
                errs.append(f"{it[1]} has no label")
            if it[5].get("grid") and not self.texts:
                errs.append(f"{it[1]} is in a grid but the panel has no header text")
        ids = [it[1] for it in self.items]
        if len(set(ids)) != len(ids):
            errs.append("duplicate ids")
        return errs

    # output ----------------------------------------------------------------
    def svg(self):
        w = self.w
        out = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{w:.2f}mm" height="{H}mm" viewBox="0 0 {w:.2f} {H}">']
        out.append(f'<rect x="0" y="0" width="{w:.2f}" height="{H}" fill="{GROUND}"/>')
        out.append(f'<rect x="1.0" y="1.0" width="{w - 2.0:.2f}" height="{H - 2.0}" fill="none" stroke="{INK}" stroke-width="0.3"/>')
        ink, white, accent, g = [], [], [], []
        # title: short name in the accent colour, description under it
        cap = TITLE_CAP
        while BOLD.width(self.slug, cap) > w - 6:
            cap -= 0.1
        accent.append(BOLD.path(self.slug, 3.5, 4.2 + cap, cap, "start"))
        dcap = DESC_CAP
        while REG.width(self.desc, dcap) > w - 6:
            dcap -= 0.05
        ink.append(REG.path(self.desc, 3.5, 4.2 + cap + 2.0 + dcap, dcap, "start"))
        foot = "F A L L E N"
        ink.append(REG.path(foot, w / 2, 125.6, 1.6))
        for (x0, y0, x1, y1) in self.lines:
            g.append(f'<line x1="{x0:.2f}" y1="{y0:.2f}" x2="{x1:.2f}" y2="{y1:.2f}" stroke="{INK}" stroke-width="0.3"/>')
        # output plates behind output jacks and their labels
        for it in self.items:
            kind, pid, label, x, y, ex = it
            if kind == "jack" and ex["out"]:
                lines = label.split("\n") if label else []
                top = y - JACK_R - 0.8
                bot = y + JACK_R + 0.8 + (len(lines) * (LABEL_CAP + 0.7) + 0.6 if lines else 0)
                wl = max(2 * JACK_R + 1.6, (max(text_width(l, LABEL_CAP) for l in lines) + 1.6) if lines else 0)
                g.append(f'<rect x="{x - wl / 2:.2f}" y="{top:.2f}" width="{wl:.2f}" height="{bot - top:.2f}" rx="0.9" fill="{PLATE}" stroke="{ACCENT}" stroke-width="0.25"/>')
        # knob scales
        for it in self.items:
            kind, pid, label, x, y, ex = it
            if kind == "knob":
                r = KNOBS[ex["size"]][1]
                a0, a1 = math.radians(-135 - 90), math.radians(135 - 90)
                sx, sy = x + r * math.cos(a0), y + r * math.sin(a0)
                ex_, ey = x + r * math.cos(a1), y + r * math.sin(a1)
                g.append(f'<path d="M{sx:.2f},{sy:.2f} A{r},{r} 0 1 1 {ex_:.2f},{ey:.2f}" fill="none" stroke="{INK}" stroke-width="0.25"/>')
                if ex.get("bipolar"):
                    g.append(f'<line x1="{x:.2f}" y1="{y - r - 0.7:.2f}" x2="{x:.2f}" y2="{y - r + 0.4:.2f}" stroke="{INK}" stroke-width="0.3"/>')
        for (did, x0, y0, x1, y1) in self.displays:
            g.append(f'<rect x="{x0:.2f}" y="{y0:.2f}" width="{x1 - x0:.2f}" height="{y1 - y0:.2f}" rx="0.6" fill="#0d0d0d"/>')
        for (t, x, base_, cap_, anchor), it in self.all_labels():
            is_out = it[0] == "jack" and it[5]["out"]
            (white if is_out else ink).append(text_path(t, x, base_, cap_, anchor))
        for (t, x, base_, cap_, anchor) in self.texts:
            ink.append(text_path(t, x, base_, cap_, anchor))
        out.append("".join(g))
        out.append(f'<path fill="{INK}" d="{" ".join(ink)}"/>')
        out.append(f'<path fill="{PLATE_INK}" d="{" ".join(white)}"/>')
        out.append(f'<path fill="{ACCENT}" d="{" ".join(accent)}"/>')
        out.append("</svg>")
        return "\n".join(out)

    def ids(self):
        par, inp, out, lig = [], [], [], []
        for kind, pid, label, x, y, ex in self.items:
            if kind in ("knob", "switch"):
                par.append(pid)
            elif kind == "button":
                par.append(pid); lig.append(ex["light"])
            elif kind == "jack":
                (out if ex["out"] else inp).append(pid)
            elif kind == "led":
                lig.append(pid)

        def en(name, xs, tail):
            return f"\tenum {name} {{ " + "".join(x + ", " for x in xs) + tail + " };"
        return "\n".join([f"struct {self.slug}Ids {{",
                          en("ParamId", par, "PARAMS_LEN"), en("InputId", inp, "INPUTS_LEN"),
                          en("OutputId", out, "OUTPUTS_LEN"), en("LightId", lig, "LIGHTS_LEN"), "};", ""])

    def cpp(self):
        lines = [f"template <class M, class W>\ninline void place{self.slug}(W* w, M* m) {{"]
        for kind, pid, label, x, y, ex in self.items:
            v = f"mm2px(Vec({x:.3f}f, {y:.3f}f))"
            if kind == "knob":
                lines.append(f"\tw->addParam(createParamCentered<{KNOBS[ex['size']][3]}>({v}, m, M::{pid}));")
            elif kind == "switch":
                cls = "FallenToggle2" if len(ex["positions"]) == 2 else "FallenToggle3"
                lines.append(f"\tw->addParam(createParamCentered<{cls}>({v}, m, M::{pid}));")
            elif kind == "button":
                cls = "FallenLatch" if ex["latch"] else "FallenButton"
                lines.append(f"\tw->addParam(createLightParamCentered<{cls}>({v}, m, M::{pid}, M::{ex['light']}));")
            elif kind == "jack":
                if ex["out"]:
                    lines.append(f"\tw->addOutput(createOutputCentered<FallenJack>({v}, m, M::{pid}));")
                else:
                    lines.append(f"\tw->addInput(createInputCentered<FallenJack>({v}, m, M::{pid}));")
            elif kind == "led":
                lines.append(f"\tw->addChild(createLightCentered<SmallLight<FallenLight>>({v}, m, M::{pid}));")
        lines.append("}")
        for (did, x0, y0, x1, y1) in self.displays:
            lines.append(f"static const float {self.slug}_{did}_MM[4] = {{{x0:.3f}f, {y0:.3f}f, {x1 - x0:.3f}f, {y1 - y0:.3f}f}};")
        lines.append("")
        return "\n".join(lines)


P = []


def cols(n, x0, x1):
    if n == 1:
        return [(x0 + x1) / 2]
    return [x0 + i * (x1 - x0) / (n - 1) for i in range(n)]


# ================================================================ panels ======
# ---- DURA (36 HP) ------------------------------------------------------------
p = Panel("DURA", "duration sequencer", 36)
p.display("BARS", 4.0, 16.5, p.w - 4.0, 27.5)
CX = [33.0 + i * 9.35 for i in range(16)]
for i, x in enumerate(CX):
    p.text(str(i + 1), x, 32.5)
for i, x in enumerate(CX):
    p.knob(f"LEN{i + 1}_PARAM", f"Step {i + 1} length", x, 38.5, size="X", grid=True)
for i, x in enumerate(CX):
    p.knob(f"REP{i + 1}_PARAM", f"Step {i + 1} repeats", x, 49.0, size="X", grid=True)
for i, x in enumerate(CX):
    p.button(f"ON{i + 1}_PARAM", f"ON{i + 1}_LIGHT", f"Step {i + 1} on", x, 59.0, latch=True, grid=True)
p.text("STEP", 25.5, 32.5, LABEL_CAP, "end")
p.text("LEN", 25.5, 39.4, LABEL_CAP, "end")
p.text("REP", 25.5, 49.9, LABEL_CAP, "end")
p.text("ON", 25.5, 59.9, LABEL_CAP, "end")
p.line(4.0, 65.5, p.w - 4.0, 65.5)
p.switch("MODE_PARAM", ["MS", "UNITS", "RATIO"], 8.0, 80.0, "MODE")
p.knob("UNIT_PARAM", "UNIT", 27.0, 80.0, "S")
p.knob("BASE_PARAM", "BASE", 40.0, 80.0, "S")
p.knob("STRETCH_PARAM", "STRETCH", 56.0, 80.0, "L")
p.knob("STEPS_PARAM", "STEPS", 72.0, 80.0, "S")
p.knob("NUDGE_PARAM", "NUDGE", 85.0, 80.0, "S", bipolar=True)
p.knob("LENGTH_PARAM", "LENGTH", 99.0, 80.0, "S")
p.knob("SPREAD_PARAM", "SPREAD", 113.0, 80.0, "S")
p.knob("SEED_PARAM", "SEED", 126.0, 80.0, "S")
p.switch("EVOLVE_PARAM", ["LOOP", "EVOLVE"], 137.5, 80.0, "VARY")
p.button("RUN_PARAM", "RUN_LIGHT", "RUN", 162.0, 80.0, latch=True)
p.button("RESET_PARAM", "RESET_LIGHT", "RESET", 174.0, 80.0)
ins = [("CLOCK_INPUT", "CLOCK"), ("RUN_INPUT", "RUN"), ("RESET_INPUT", "RESET"), ("STRETCH_INPUT", "STRETCH\nCV"),
       ("LENGTH_INPUT", "LENGTH\nCV"), ("SPREAD_INPUT", "SPREAD\nCV"), ("STEPS_INPUT", "STEPS\nCV")]
for (pid, lab), x in zip(ins, [9.0 + 12.5 * i for i in range(7)]):
    p.jack(pid, lab, x, 102.0)
outs = [("TRIG_OUTPUT", "TRIG"), ("GATE_OUTPUT", "GATE"), ("STEP_OUTPUT", "STEP"), ("INTERVAL_OUTPUT", "INTERVAL"),
        ("REPN_OUTPUT", "REP"), ("EOC_OUTPUT", "EOC")]
for (pid, lab), x in zip(outs, [112.0 + 13.0 * i for i in range(6)]):
    p.jack(pid, lab, x, 102.0)
P.append(p)

# ---- GROUP (12 HP) -----------------------------------------------------------
p = Panel("GROUP", "burst generator", 12)
X3 = [11.5, 30.48, 49.46]
p.knob("COUNT_PARAM", "COUNT", X3[0], 27.0)
p.knob("SPACE_PARAM", "SPACE", X3[1], 27.0)
p.knob("CURVE_PARAM", "CURVE", X3[2], 27.0, bipolar=True)
p.knob("PERIOD_PARAM", "PERIOD", X3[0], 45.0)
p.knob("LENGTH_PARAM", "LENGTH", X3[1], 45.0)
p.button("FIRE_PARAM", "FIRE_LIGHT", "FIRE", X3[2], 45.5)
p.knob("RATE_PARAM", "RATE", X3[0], 62.5, "S")
p.knob("DEPTH_PARAM", "DEPTH", X3[1], 62.5, "S")
X4 = [8.5, 22.5, 38.5, 52.5]
for (pid, lab), x in zip([("TRIG_INPUT", "TRIG IN"), ("RESET_INPUT", "RESET"), ("COUNT_INPUT", "COUNT\nCV"), ("SPACE_INPUT", "SPACE\nCV")], X4):
    p.jack(pid, lab, x, 78.0)
for (pid, lab), x in zip([("CURVE_INPUT", "CURVE\nCV"), ("PERIOD_INPUT", "PERIOD\nCV"), ("TRIG_OUTPUT", "TRIG"), ("GATE_OUTPUT", "GATE")], X4):
    p.jack(pid, lab, x, 94.0)
for (pid, lab), x in zip([("FIRST_OUTPUT", "FIRST"), ("LAST_OUTPUT", "LAST"), ("INDEX_OUTPUT", "INDEX"), ("BUSY_OUTPUT", "BUSY")], X4):
    p.jack(pid, lab, x, 110.0)
P.append(p)

# ---- PRIME (18 HP) -----------------------------------------------------------
p = Panel("PRIME", "nested prime loops", 18)
p.display("DOTS", 4.0, 16.5, p.w - 4.0, 32.0)
RY = [43.0, 53.0, 63.0, 73.0]
for t, x in [("LEN", 15.0), ("HITS", 26.0), ("ROT", 37.0), ("SOURCE", 51.0), ("TRIG", 71.0), ("GATE", 83.0)]:
    p.text(t, x, 37.6, LABEL_CAP)
for r, y in enumerate(RY):
    p.text("ABCD"[r], 6.0, y + 0.9, LABEL_CAP)
for k, (nm, x) in enumerate([("LEN", 15.0), ("HITS", 26.0), ("ROT", 37.0)]):
    for r, y in enumerate(RY):
        L = "ABCD"[r]
        p.knob(f"{nm}{L}_PARAM", f"Row {L} {nm.lower()}", x, y, "S", grid=True)
for r, y in enumerate(RY):
    if r > 0:
        p.switch(f"SOURCE{'ABCD'[r]}_PARAM", ["PULSE", "NEST"], 46.0, y)
for r, y in enumerate(RY):
    p.led(f"ROW{'ABCD'[r]}_LIGHT", f"Row {'ABCD'[r]}", 62.5, y, grid=True)
for r, y in enumerate(RY):
    L = "ABCD"[r]
    p.jack(f"TRIG{L}_OUTPUT", f"TRIG {L}", 71.0, y, grid=True)
for r, y in enumerate(RY):
    L = "ABCD"[r]
    p.jack(f"GATE{L}_OUTPUT", f"GATE {L}", 83.0, y, grid=True)
p.line(4.0, 80.0, p.w - 4.0, 80.0)
p.knob("RATE_PARAM", "RATE", 12.0, 92.0)
p.button("RUN_PARAM", "RUN_LIGHT", "RUN", 27.0, 92.0, latch=True)
p.button("RESET_PARAM", "RESET_LIGHT", "RESET", 39.0, 92.0)
p.jack("ANY_OUTPUT", "ANY", 71.0, 92.0)
p.jack("REALIGN_OUTPUT", "REALIGN", 83.5, 92.0)
for (pid, lab), x in zip([("CLOCK_INPUT", "CLOCK"), ("RESET_INPUT", "RESET"), ("RUN_INPUT", "RUN"), ("RATE_INPUT", "RATE CV")], [10.0, 23.0, 36.0, 50.0]):
    p.jack(pid, lab, x, 109.0)
P.append(p)

# ---- LANES (32 HP) -----------------------------------------------------------
p = Panel("LANES", "drum grid", 32)
p.display("GRID", 26.0, 19.0, p.w - 4.0, 79.0)
p.text("MUTE", 6.0, 17.8)
p.text("LEN", 13.5, 17.8)
p.text("SCROLL", 21.0, 17.8)
for r in range(6):
    p.button(f"MUTE{r + 1}_PARAM", f"MUTE{r + 1}_LIGHT", f"Row {r + 1} mute", 6.0, 24.0 + 10.0 * r, latch=True, grid=True)
for r in range(6):
    p.knob(f"LEN{r + 1}_PARAM", f"Row {r + 1} loop length", 13.5, 24.0 + 10.0 * r, "X", grid=True)
for r in range(6):
    p.knob(f"SCROLL{r + 1}_PARAM", f"Row {r + 1} scroll per loop", 21.0, 24.0 + 10.0 * r, "X", bipolar=True, grid=True)
p.knob("INTENSITY_PARAM", "INTENSITY", 12.0, 95.0)
p.knob("PATTERN_PARAM", "PATTERN", 31.0, 95.0)
p.button("RESET_PARAM", "RESET_LIGHT", "RESET", 47.0, 95.0)
for r, x in enumerate([66.0, 80.0, 94.0, 108.0, 122.0]):
    p.jack(f"TRIG{r + 1}_OUTPUT", f"TRIG {r + 1}", x, 95.0)
p.jack("GATES_OUTPUT", "GATES", 136.0, 95.0)
for (pid, lab), x in zip([("CLOCK_INPUT", "CLOCK"), ("RESET_INPUT", "RESET"), ("INTENSITY_INPUT", "INTENSITY\nCV"), ("PATTERN_INPUT", "PATTERN\nCV")], [9.0, 22.0, 36.5, 51.5]):
    p.jack(pid, lab, x, 110.0)
p.jack("PITCH_OUTPUT", "PITCH", 136.0, 110.0)
p.jack("PITCHGATE_OUTPUT", "PITCH\nGATE", 150.0, 110.0)
P.append(p)

# ---- CYCLE (16 HP) -----------------------------------------------------------
p = Panel("CYCLE", "phase lanes", 16)
p.display("BARS", 4.0, 16.5, p.w - 4.0, 55.0)
for t, x in [("LEN", 14.0), ("DIV", 25.0), ("OFFSET", 36.5), ("MODE", 49.0), ("OUT", 67.0)]:
    p.text(t, x, 60.5, LABEL_CAP)
for r in range(4):
    p.text("ABCD"[r], 5.5, 67.0 + 10.5 * r + 0.9, LABEL_CAP)
for nm, x, bip in [("LEN", 14.0, False), ("DIV", 25.0, False), ("OFFSET", 36.5, True), ("MODE", 49.0, False)]:
    for r in range(4):
        L = "ABCD"[r]
        p.knob(f"{nm}{L}_PARAM", f"Lane {L} {nm.lower()}", x, 67.0 + 10.5 * r, "S", bipolar=bip, grid=True)
for r in range(4):
    L = "ABCD"[r]
    p.jack(f"OUT{L}_OUTPUT", f"OUT {L}", 67.0, 67.0 + 10.5 * r, grid=True)
for (pid, lab), x in zip([("TRIG_INPUT", "TRIG"), ("RESET_INPUT", "RESET")], [9.0, 22.0]):
    p.jack(pid, lab, x, 112.0)
p.button("RESET_PARAM", "RESET_LIGHT", "RESET", 36.0, 113.0)
p.jack("POLY_OUTPUT", "POLY", 67.0, 112.0)
P.append(p)

# ---- HOLD (6 HP) -------------------------------------------------------------
p = Panel("HOLD", "sustain against gap", 6)
p.knob("LENGTH_PARAM", "LENGTH", 15.24, 26.0)
p.knob("ATTACK_PARAM", "ATTACK", 8.5, 43.0, "S")
p.knob("SHAPE_PARAM", "SHAPE", 22.0, 43.0, "S")
XH = [8.5, 22.0]
p.jack("TRIG_INPUT", "TRIG", XH[0], 58.0)
p.jack("INTERVAL_INPUT", "INTERVAL", XH[1], 58.0)
p.jack("LENGTH_INPUT", "LEN CV", XH[0], 73.0)
p.jack("IN_INPUT", "IN", XH[1], 73.0)
p.jack("GATE_OUTPUT", "GATE", XH[0], 92.0)
p.jack("ENV_OUTPUT", "ENV", XH[1], 92.0)
p.jack("OUT_OUTPUT", "OUT", 15.24, 108.0)
P.append(p)

# ---- FM4 (28 HP) -------------------------------------------------------------
p = Panel("FM4", "four-operator FM voice", 28)
p.display("SCREEN", 4.0, 16.5, 70.0, 36.0)
p.knob("VOICE_PARAM", "VOICE", 81.0, 28.0)
p.switch("STEP_PARAM", ["OFF", "ON"], 94.5, 28.5, "STEP")
p.knob("FIRST_PARAM", "FIRST", 110.0, 28.0, "S")
p.knob("COUNT_PARAM", "COUNT", 122.5, 28.0, "S")
p.knob("LEVEL_PARAM", "LEVEL", 135.0, 28.0, "S")
MX = [10.0, 32.5, 55.0, 77.0, 101.5, 124.0]
for (k, lab), x in zip([("BRIGHT", "BRIGHT"), ("RATIO", "RATIO"), ("DECAY", "DECAY"), ("ATTACK", "ATTACK"), ("FDBK", "FDBK"), ("SPREAD", "SPREAD")], MX):
    p.knob(f"{k}_PARAM", lab, x, 48.0, bipolar=(k != "SPREAD"))
    if k != "SPREAD":
        p.jack(f"{k}_INPUT", f"{lab}\nCV", x + 11.5, 48.0)
p.line(4.0, 64.0, p.w - 4.0, 64.0)
p.text("OPERATOR EDIT", 4.0, 68.6, LABEL_CAP, "start")
for i, x in enumerate([8.0, 17.0, 26.0, 35.0]):
    p.button(f"OP{i + 1}_PARAM", f"OP{i + 1}_LIGHT", f"OP {i + 1}", x, 77.0)
p.knob("ALG_PARAM", "ALG", 47.0, 77.0, "S")
p.knob("FB_PARAM", "FB", 59.0, 77.0, "S")
EX = [74.0, 86.0, 98.0, 110.0, 122.0, 134.0]
for (k, lab), x in zip([("CRS", "CRS"), ("FINE", "FINE"), ("OPLEVEL", "LEVEL"), ("WAVE", "WAVE"), ("DET", "DET")], EX):
    p.knob(f"{k}_PARAM", lab, x, 77.0, "S")
for (k, lab), x in zip([("AR", "AR"), ("D1R", "D1R"), ("D1L", "D1L"), ("D2R", "D2R"), ("RR", "RR")], EX):
    p.knob(f"{k}_PARAM", lab, x, 91.0, "S")
ins = [("VOCT_INPUT", "V/OCT"), ("GATE_INPUT", "GATE"), ("VEL_INPUT", "VEL"), ("VOICE_INPUT", "VOICE\nCV"), ("STEPRESET_INPUT", "STEP\nRESET")]
for (pid, lab), x in zip(ins, [8.0, 20.5, 33.0, 45.5, 58.0]):
    p.jack(pid, lab, x, 106.0)
p.jack("OUT_OUTPUT", "OUT", 118.0, 106.0)
p.jack("POLY_OUTPUT", "POLY\nOUT", 132.0, 106.0)
P.append(p)

# ---- CHORD (10 HP) -----------------------------------------------------------
p = Panel("CHORD", "chord memory", 10)
p.display("KEYS", 3.0, 16.5, p.w - 3.0, 36.0)
p.knob("CHORD_PARAM", "CHORD", 11.5, 49.0)
p.knob("INVERT_PARAM", "INVERT", 27.0, 49.0, "S")
p.switch("VOICING_PARAM", ["CLOSE", "DROP2", "SPREAD"], 36.5, 49.5, "VOICE")
p.knob("TRANSPOSE_PARAM", "TRANSPOSE", 11.5, 66.0, "S", bipolar=True)
p.jack("ROOT_INPUT", "ROOT", 27.0, 66.0)
p.jack("GATEIN_INPUT", "GATE IN", 41.0, 66.0)
p.jack("CHORD_INPUT", "CHORD\nCV", 11.5, 84.0)
p.jack("INVERT_INPUT", "INVERT\nCV", 27.0, 84.0)
p.jack("VOCT_OUTPUT", "V/OCT", 18.0, 106.0)
p.jack("GATE_OUTPUT", "GATE", 34.0, 106.0)
P.append(p)

# ---- KIT (30 HP) -------------------------------------------------------------
p = Panel("KIT", "drum machine", 30)
VX = [12.0, 34.0, 56.0, 78.0, 100.0, 122.0]
NAMES = ["KICK", "SNARE", "CLAP", "C.HAT", "O.HAT", "PERC"]
for i, (x, name) in enumerate(zip(VX, NAMES)):
    p.text(name, x, 19.0, 2.0)
    p.display(f"NAME{i + 1}", x - 9.5, 20.5, x + 9.5, 25.5)
    if i < 5:
        p.line(x + 11.0, 18.0, x + 11.0, 120.0)
for i, x in enumerate(VX):
    p.switch(f"SRC{i + 1}_PARAM", ["EARLY", "ANALOG", "SAMPLE"], x - 5.0, 34.0)
for i, x in enumerate(VX):
    p.knob(f"TUNE{i + 1}_PARAM", "TUNE", x, 49.0, "S", bipolar=True)
for i, x in enumerate(VX):
    p.knob(f"DECAY{i + 1}_PARAM", "DECAY", x, 61.5, "S")
for i, x in enumerate(VX):
    p.knob(f"LEVEL{i + 1}_PARAM", "LEVEL", x, 74.0, "S")
for i, x in enumerate(VX):
    p.jack(f"TRIG{i + 1}_INPUT", "TRIG", x, 86.0)
for i, x in enumerate(VX):
    p.jack(f"TUNE{i + 1}_INPUT", "TUNE CV", x, 99.5)
for i, x in enumerate(VX):
    p.jack(f"OUT{i + 1}_OUTPUT", "OUT", x, 112.5)
p.line(133.0, 18.0, 133.0, 120.0)
p.knob("CRUNCH_PARAM", "CRUNCH", 142.5, 34.0, "S")
p.jack("ACCENT_INPUT", "ACCENT", 142.5, 86.0)
p.jack("MIX_OUTPUT", "MIX", 142.5, 112.5)
P.append(p)

# ---- SPECTRA (14 HP) ---------------------------------------------------------
p = Panel("SPECTRA", "additive spectrum", 14)
SX = [12.5, 35.56, 58.6]
knobs = [("PARTIALS", "PARTIALS", False), ("STRETCH", "STRETCH", True), ("TILT", "TILT", True),
         ("ODDEVEN", "ODD/EVEN", True), ("RATE", "RATE", False), ("DEPTH", "DEPTH", False),
         ("DETUNE", "DETUNE", False), ("WIDTH", "WIDTH", False), ("LEVEL", "LEVEL", False),
         ("ATTACK", "ATTACK", False), ("RELEASE", "RELEASE", False)]
for i, (k, lab, bip) in enumerate(knobs):
    p.knob(f"{k}_PARAM", lab, SX[i % 3], 26.0 + 16.5 * (i // 3), bipolar=bip)
JX = [9.5, 26.5, 44.5, 61.5]
for (pid, lab), x in zip([("STRETCH_INPUT", "STRETCH\nCV"), ("TILT_INPUT", "TILT\nCV"), ("DEPTH_INPUT", "DEPTH\nCV"), ("PARTIALS_INPUT", "PARTIALS\nCV")], JX):
    p.jack(pid, lab, x, 93.0)
for (pid, lab), x in zip([("VOCT_INPUT", "V/OCT"), ("GATE_INPUT", "GATE"), ("L_OUTPUT", "L"), ("R_OUTPUT", "R")], JX):
    p.jack(pid, lab, x, 110.0)
P.append(p)


# ---------------------------------------------------------------- components
def components():
    c = {}
    nut = []
    for i in range(6):
        a = math.pi / 3 * i + math.pi / 6
        nut.append(f"{4 + 3.5 * math.cos(a):.3f},{4 + 3.5 * math.sin(a):.3f}")
    c["FallenJack"] = f"""<svg xmlns="http://www.w3.org/2000/svg" width="8mm" height="8mm" viewBox="0 0 8 8">
<circle cx="4" cy="4" r="3.9" fill="#c8c8c4"/>
<polygon points="{" ".join(nut)}" fill="#1a1a1a"/>
<circle cx="4" cy="4" r="2.3" fill="#9a9a96"/>
<circle cx="4" cy="4" r="1.65" fill="#050505"/>
</svg>"""

    def knob(name, r, line_w):
        d = 2 * r
        c[name] = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{d}mm" height="{d}mm" viewBox="0 0 {d} {d}">
<circle cx="{r}" cy="{r}" r="{r}" fill="#121212"/>
<line x1="{r}" y1="{r * 0.12:.3f}" x2="{r}" y2="{r * 0.78:.3f}" stroke="#f1f1ee" stroke-width="{line_w}" stroke-linecap="round"/>
</svg>"""
    knob("FallenKnob", KNOBS["L"][0], 0.6)
    knob("FallenKnobSmall", KNOBS["S"][0], 0.5)
    knob("FallenKnobTiny", KNOBS["X"][0], 0.45)

    for state in (0, 1):
        s = 2 * BTN_R
        c[f"FallenButton_{state}"] = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{s}mm" height="{s}mm" viewBox="0 0 {s} {s}">
<rect x="0.1" y="0.1" width="{s - 0.2:.2f}" height="{s - 0.2:.2f}" rx="0.7" fill="{'#2a2a2a' if state else '#121212'}"/>
</svg>"""

    def toggle(d):
        cx, cy = SW_W / 2, SW_H / 2
        ty = cy + d * 2.6
        return f"""<svg xmlns="http://www.w3.org/2000/svg" width="{SW_W}mm" height="{SW_H}mm" viewBox="0 0 {SW_W} {SW_H}">
<rect x="0.2" y="0.2" width="{SW_W - 0.4}" height="{SW_H - 0.4}" rx="0.6" fill="#121212"/>
<rect x="{cx - 1.2:.2f}" y="{ty - 1.3:.2f}" width="2.4" height="2.6" rx="0.4" fill="#f1f1ee"/>
</svg>"""
    c["Toggle2_0"] = toggle(-1)
    c["Toggle2_1"] = toggle(1)
    c["Toggle3_0"] = toggle(-1)
    c["Toggle3_1"] = toggle(0)
    c["Toggle3_2"] = toggle(1)
    return c


def main():
    errs = []
    for p in P:
        for e in p.check():
            errs.append(f"{p.slug}: {e}")
    if errs:
        print("\n".join(errs))
        raise SystemExit(f"{len(errs)} layout problems")
    os.makedirs(os.path.join(ROOT, "res", "components"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "src", "generated"), exist_ok=True)
    for p in P:
        with open(os.path.join(ROOT, "res", f"{p.slug}.svg"), "w") as f:
            f.write(p.svg())
    for name, svg in components().items():
        with open(os.path.join(ROOT, "res", "components", f"{name}.svg"), "w") as f:
            f.write(svg)
    with open(os.path.join(ROOT, "src", "generated", "Layout.hpp"), "w") as f:
        f.write("// Generated by scripts/make_panels.py. Do not edit.\n#pragma once\n#include \"../widgets/FallenComponents.hpp\"\n\n")
        for p in P:
            f.write(p.ids())
            f.write(p.cpp())
    # tooltip names for grid controls (they have no printed label of their own)
    print(f"wrote {len(P)} panels")


if __name__ == "__main__":
    main()
