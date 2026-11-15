"""Compose each panel with its knobs, jacks, switches and buttons into docs/img/<Slug>.svg for the manual.
Then scripts/manual/topng.js renders PNGs. Needs only Python 3."""
import os, re
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
lay = open(os.path.join(ROOT, "src/generated/Layout.hpp")).read()
COMP = {"FallenKnobSmall": "FallenKnobSmall", "FallenKnobTiny": "FallenKnobTiny", "FallenKnob": "FallenKnob",
        "FallenToggle2": "Toggle2_0", "FallenToggle3": "Toggle3_0", "FallenJack": "FallenJack",
        "FallenButton": "FallenButton_0", "FallenLatch": "FallenButton_0"}

def inner(svg):
    m = re.search(r'viewBox="([^"]+)"', svg)
    body = re.sub(r"^.*?<svg[^>]*>", "", svg, flags=re.S)
    body = re.sub(r"</svg>\s*$", "", body.strip())
    return m.group(1), body

os.makedirs(os.path.join(ROOT, "docs/img"), exist_ok=True)
for blk in re.finditer(r"inline void place(\w+)\(W\* w, M\* m\) \{(.*?)\n\}", lay, re.S):
    slug, body = blk.group(1), blk.group(2)
    panel = open(os.path.join(ROOT, "res", slug + ".svg")).read()
    W, H = re.search(r'width="([\d.]+)mm" height="([\d.]+)mm"', panel).groups()
    vb, pbody = inner(panel)
    parts = [f'<svg xmlns="http://www.w3.org/2000/svg" width="{W}mm" height="{H}mm" viewBox="{vb}">', pbody]
    for line in body.splitlines():
        m = re.search(r"Vec\(([\d.]+)f, ([\d.]+)f\)", line)
        if not m:
            continue
        x, y = float(m.group(1)), float(m.group(2))
        cls = None
        for k in sorted(COMP, key=len, reverse=True):
            if f"<{k}>" in line:
                cls = k; break
        if cls:
            csvg = open(os.path.join(ROOT, "res/components", COMP[cls] + ".svg")).read()
            cw, ch = map(float, re.search(r'width="([\d.]+)mm" height="([\d.]+)mm"', csvg).groups())
            cvb, cbody = inner(csvg)
            parts.append(f'<svg x="{x - cw / 2:.3f}" y="{y - ch / 2:.3f}" width="{cw}" height="{ch}" viewBox="{cvb}">{cbody}</svg>')
        elif "SmallLight" in line:
            parts.append(f'<circle cx="{x}" cy="{y}" r="1.0" fill="#555"/>')
    for m in re.finditer(r"static const float " + slug + r"_\w+_MM\[4\] = \{([^}]*)\}", lay):
        x, y, w, h = [float(v.strip().rstrip("f")) for v in m.group(1).split(",")]
        parts.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="0.6" fill="#0d0d0d"/>')
    parts.append("</svg>")
    open(os.path.join(ROOT, "docs/img", slug + ".svg"), "w").write("\n".join(parts))
print("ok")
