"""Graphical items: the line type and the colour of their border are properties.

Rectangle, Ellipse, Image and Text drew their border with a hard-coded black, solid pen, and a
Line was always solid: only the width (Border) could be set.

Now each of the five has Border_Style (Solid, Dash, Dot, DashDot, DashDotDot; default Solid) and,
except Line, whose colour is its Color, Border_Color (default #000000: black, as before, so saved
circuits look the same). They are set in the properties dialog (Line Type under Border, Border
Color next to the fill colour) and from the CLI, and kept by save / reopen, undo and copy / paste.

Right result: a Solid border is one stroke, so it has more pixels of the border colour than any
dashed style. Of the dashed styles, Dash (dash 4, gap 2) has more than Dot (dash 1, gap 2): per
unit of length their dashes are 4/6 and 1/3 long, and whatever the round caps add to each dash
(at most 1 unit) leaves Dash ahead (4 + c against 2 + 2c). The border colour replaces the black
(a default border is black on a light canvas), and nothing of it is drawn by an item that keeps
the default.
"""

import os
import re
import struct
import tempfile
import zlib

from harness import SimError, run_test

STYLES = ["Solid", "Dash", "Dot", "DashDot", "DashDotDot"]
COLORS = {"Rectangle": "#ff0000", "Ellipse": "#0000ff", "Line": "#008000", "Text": "#800080"}
RGB = {kind: tuple(int(c[i:i + 2], 16) for i in (1, 3, 5)) for kind, c in COLORS.items()}   # "#rrggbb" -> (r, g, b)
BLACK = (0, 0, 0)
CIRCUIT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "circuits", "border_styles.sim1")


def read_png(path):
    """Pixels of an 8-bit RGB / RGBA PNG (what the CLI writes): rows of (r, g, b)."""
    data = open(path, "rb").read()
    pos, idat = 8, b""
    while pos < len(data):
        length, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if kind == b"IHDR":
            w, h, depth, ctype = struct.unpack(">IIBB", body[:10])
        elif kind == b"IDAT":
            idat += body
    bpp = 4 if ctype == 6 else 3                 # RGBA, else RGB (no palette or greyscale from Qt)
    stride = w * bpp
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(h):                           # Undo the per-row filters 0 (none) to 4 (Paeth)
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        rows.append([tuple(line[x * bpp:x * bpp + 3]) for x in range(w)])
    return rows


def prop(sim, uid, name):
    """A property as the CLI reports it ({} when the item has no such property)."""
    for p in sim.cli("comp", "get", uid)["properties"]:
        if p["name"] == name:
            return p
    return {}


def is_str(sim, name, value, expected):
    sim.check(name, int(value == expected), 1, absolute=0, note=repr(value))


def refuse(sim, name, *args):
    """The CLI must refuse this command (it raises SimError)."""
    try:
        sim.cli(*args)
    except SimError:
        sim.check(name, 1, 1, absolute=0)
        return
    sim.check(name, 0, 1, absolute=0)


def colored(sim, tmp, uid, rgb):
    """Pixels exactly of a colour in a 4x capture of one component."""
    png = os.path.join(tmp, "capture.png")
    sim.cli("capture", "circuit", uid, "--scale", "4", "--margin", "6", "--file", png)
    return sum(1 for row in read_png(png) for px in row if px == rgb)


def check_ink(sim, what, counts):
    """Solid has the most ink, every dashed style still draws a border, Dash more than Dot."""
    sim.check("%s: a solid border is drawn in its colour" % what, int(counts[0] > 100), 1, absolute=0)
    for style, count in zip(STYLES[1:], counts[1:]):
        sim.check("%s %s: fewer pixels than Solid" % (what, style), int(count < counts[0]), 1, absolute=0)
        sim.check("%s %s: still at least 35 %% of Solid" % (what, style), int(count >= 0.35 * counts[0]), 1, absolute=0)
    sim.check("%s: Dash has more ink than Dot" % what, int(counts[1] > counts[2]), 1, absolute=0)


def add(sim, kind, n, *more):
    """A graphical item alone in a 400 px cell (so a capture shows only it), by cell number."""
    return sim.cli("comp", "add", kind, "--at", "%d,%d" % ((n % 5) * 400, (n // 5) * 400), *more)["uid"]


def test(sim):
    tmp = tempfile.mkdtemp(prefix="gfx-border-")
    sim.cli("circuit", "new", "--discard")

    # --- The properties, their defaults and what they accept -----------------------------

    defaults = {kind: add(sim, kind, 25 + i) for i, kind in enumerate(COLORS)}
    for kind, uid in defaults.items():
        is_str(sim, "%s: default Border_Style" % kind, prop(sim, uid, "Border_Style").get("value"), "Solid")
        is_str(sim, "%s: the dialog names it Line Type" % kind, prop(sim, uid, "Border_Style").get("caption"), "Line Type")
        if kind == "Line":
            sim.check("Line: no Border_Color (its colour is Color)", int(not prop(sim, uid, "Border_Color")), 1, absolute=0)
        else:
            is_str(sim, "%s: default Border_Color is black" % kind, prop(sim, uid, "Border_Color").get("value"), "#000000")
    is_str(sim, "Border_Style options", "|".join(prop(sim, defaults["Rectangle"], "Border_Style").get("options") or []),
           "|".join(STYLES))
    refuse(sim, "Line: Border_Color is refused", "comp", "set", defaults["Line"], "Border_Color=#ff0000")

    rect = defaults["Rectangle"]
    for value, wanted in [("dashed", "Dash"), ("DOT", "Dot"), ("3", "DashDot"), ("dash-dot-dot", "DashDotDot")]:
        is_str(sim, "Border_Style=%s" % value, sim.cli("comp", "set", rect, "Border_Style=" + value).get("Border_Style"), wanted)
    refuse(sim, "Border_Style=zigzag is refused", "comp", "set", rect, "Border_Style=zigzag")
    is_str(sim, "a refused value changes nothing", prop(sim, rect, "Border_Style").get("value"), "DashDotDot")

    # --- Drawn: the colour of the border, and the line type --------------------------------

    for kind in COLORS:
        uids = [add(sim, kind, i, "Border=2", "Border_Style=" + s,
                    *(["Color=" + COLORS[kind]] if kind == "Line" else ["Border_Color=" + COLORS[kind]])) for i, s in enumerate(STYLES)]
        counts = [colored(sim, tmp, uid, RGB[kind]) for uid in uids]
        sim.log("  %s, pixels of the border colour %s: %s" % (kind, COLORS[kind], dict(zip(STYLES, counts))))
        check_ink(sim, kind, counts)
        if kind != "Line":
            sim.check("%s: nothing of that colour with the default black" % kind, colored(sim, tmp, defaults[kind], RGB[kind]), 0, absolute=0)

    # --- Kept: save / reopen, undo / redo, copy / paste --------------------------------------

    ellipse, line = defaults["Ellipse"], defaults["Line"]
    sim.cli("comp", "set", ellipse, "Border=2", "Border_Color=" + COLORS["Ellipse"], "Border_Style=DashDot")
    sim.cli("comp", "set", line, "Border_Style=Dot")
    file = os.path.join(tmp, "border.sim1")
    sim.cli("circuit", "save", file)
    sim.cli("circuit", "new", "--discard")
    sim.cli("circuit", "open", file)
    is_str(sim, "reopen: Ellipse Border_Color", prop(sim, ellipse, "Border_Color").get("value"), COLORS["Ellipse"])
    is_str(sim, "reopen: Ellipse Border_Style", prop(sim, ellipse, "Border_Style").get("value"), "DashDot")
    is_str(sim, "reopen: Line Border_Style", prop(sim, line, "Border_Style").get("value"), "Dot")

    sim.cli("comp", "set", ellipse, "Border_Color=#00ff00", "Border_Style=Solid")
    sim.cli("undo")
    is_str(sim, "undo: Border_Color back", prop(sim, ellipse, "Border_Color").get("value"), COLORS["Ellipse"])
    is_str(sim, "undo: Border_Style back", prop(sim, ellipse, "Border_Style").get("value"), "DashDot")
    sim.cli("redo")
    is_str(sim, "redo: Border_Color", prop(sim, ellipse, "Border_Color").get("value"), "#00ff00")
    is_str(sim, "redo: Border_Style", prop(sim, ellipse, "Border_Style").get("value"), "Solid")

    copy = sim.cli("comp", "copy", line, "--by", "0,200")
    is_str(sim, "copy / paste: Line Border_Style", prop(sim, copy[0], "Border_Style").get("value"), "Dot")
    sim.check("copy / paste: the copy has no Border_Color", int(not prop(sim, copy[0], "Border_Color")), 1, absolute=0)

    # Junk in a saved attribute (as the garble sweep writes it) loads as the default, without a crash
    junk = os.path.join(tmp, "junk.sim1")
    text = open(file, encoding="utf-8").read()
    text, n1 = re.subn(r'(CircId="%s"[^>]*Border_Style=")Dot"' % line, r'\1zigzag"', text)
    text, n2 = re.subn(r'(CircId="%s"[^>]*Border_Color=")#0000ff"' % ellipse, r'\1not a colour"', text)
    open(junk, "w", encoding="utf-8").write(text)
    sim.check("junk written into two saved attributes", n1 + n2, 2, absolute=0)
    sim.cli("circuit", "open", junk, "--discard")
    is_str(sim, "junk Border_Style loads as Solid", prop(sim, line, "Border_Style").get("value"), "Solid")
    is_str(sim, "junk Border_Color leaves the default black", prop(sim, ellipse, "Border_Color").get("value"), "#000000")

    # --- A default border is black, as before these properties ------------------------------

    def exported_black():
        """Black pixels of the circuit exported as PNG on a light canvas, without the grid."""
        png = os.path.join(tmp, "export.png")
        sim.cli("export", "--file", png, "--format", "png", "--theme", "Light", "--grid", "hide", "--scale", "4")
        return sum(1 for row in read_png(png) for px in row if px == BLACK)

    sim.cli("circuit", "new", "--discard")
    rect = add(sim, "Rectangle", 0)
    black = exported_black()
    sim.log("  black pixels of a default Rectangle, exported on a light canvas: %s" % black)
    sim.check("default Rectangle: its border is black", int(black > 100), 1, absolute=0)
    sim.cli("comp", "set", rect, "Border_Color=#ff0000")
    sim.check("Rectangle with a border colour: no black left", exported_black(), 0, absolute=0)

    # --- SVG export: the colour as the stroke, the line type as the dash array ---------------

    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Rectangle", "--at", "0,0", "Border_Color=" + COLORS["Rectangle"], "Border_Style=Dash")
    sim.cli("comp", "add", "Rectangle", "--at", "0,200", "Border_Color=" + COLORS["Rectangle"])
    svg = os.path.join(tmp, "border.svg")
    sim.cli("export", "--file", svg, "--format", "svg", "--theme", "Light")
    groups = re.findall(r'<g ([^>]*)>\s*<rect x="-25" y="-15" width="50" height="30"/>', open(svg, encoding="utf-8").read())
    stroked = [g for g in groups if 'stroke="#ff0000"' in g]           # The fill is a group of its own
    dashed = [g for g in stroked if 'stroke-dasharray="8,4"' in g]
    solid = [g for g in stroked if "stroke-dasharray" not in g]
    sim.log("  SVG rectangles: %s groups, stroked #ff0000 %s, of which dashed %s, solid %s" % (len(groups), len(stroked), len(dashed), len(solid)))
    sim.check("SVG: the dashed rectangle has stroke #ff0000 and dash array 8,4", len(dashed), 1, absolute=0)
    sim.check("SVG: the solid rectangle has stroke #ff0000 and no dash array", len(solid), 1, absolute=0)

    # --- The circuit kept for the GUI --------------------------------------------------------

    types = {"Text": "TextComponent"}          # comp list names the types as the items are saved
    sim.cli("circuit", "open", CIRCUIT, "--discard")
    items = [c for c in sim.cli("comp", "list") if c["type"] in [types.get(k, k) for k in COLORS]]
    for kind in COLORS:
        row = sorted((c for c in items if c["type"] == types.get(kind, kind) and c["pos"][1] < 100 and c["pos"][0] > -100),
                     key=lambda c: c["pos"][0])          # Left of x = -100: the row labels, plain Text items
        sim.check("%s row of border_styles.sim1: five styles" % kind, len(row), 5, absolute=0)
        counts = [colored(sim, tmp, c["uid"], RGB[kind]) for c in row]
        values = [prop(sim, c["uid"], "Border_Style").get("value") for c in row]
        sim.log("  %s: %s %s" % (kind, values, counts))
        is_str(sim, "%s row: the styles in order" % kind, "|".join(v or "?" for v in values), "|".join(STYLES))
        if kind == "Text":                 # Its five boxes hold different words, so their ink is not comparable
            sim.check("Text row: every box has a border in its colour", int(all(c > 0 for c in counts)), 1, absolute=0, note=str(counts))
        else:
            check_ink(sim, "%s row" % kind, counts)


if __name__ == "__main__":
    run_test(test)
