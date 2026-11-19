"""K3 (specs/keyboard-input.md): selected pin per part, < > and Cmd+< Cmd+>.

Expected values follow the spec: every part has a selected pin, by default (auto) the topmost
visible pin, then the rightmost; with one part selected > / < step clockwise / back on screen
(the order is computed here from the pin positions `comp add` reports: angle around their mean,
scene y down) and set pinTo; Cmd+> / Cmd+< set pinFrom instead; the pin stays the same through a
rotation; with two parts selected the keys do nothing. The pin colours are checked on a capture
of the view (Dark theme: highlight #ffd200). Nothing is simulated.
"""

import math
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def clockwise(pins):
    """Visible pin refs, clockwise on screen around their mean position."""
    vis = [p for p in pins if not p.get("hidden")]
    cx = sum(p["pos"][0] for p in vis) / len(vis)
    cy = sum(p["pos"][1] for p in vis) / len(vis)
    return [p["ref"] for p in sorted(vis, key=lambda p: math.atan2(p["pos"][1] - cy, p["pos"][0] - cx))]


def default_pin(pins):
    vis = [p for p in pins if not p.get("hidden")]
    return min(vis, key=lambda p: (p["pos"][1], -p["pos"][0]))["ref"]


def step(order, ref, d):
    return order[(order.index(ref) + d) % len(order)]


def yellow_pixels(sim, path):
    """Pixels of the Dark-theme highlight (#ffd200) in a view capture."""
    sim.cli("capture", "window", "view", "--file", path)
    import struct
    import zlib
    with open(path, "rb") as f:
        data = f.read()
    # Minimal PNG reader (8-bit RGB/RGBA, non-interlaced), to avoid needing Pillow
    pos, w, h, ct, idat = 8, 0, 0, 0, b""
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]
        typ = data[pos + 4:pos + 8]
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, _, ct = struct.unpack(">IIBB", body[:10])
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 4 if ct == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    prev = bytearray(stride)
    count = 0
    i = 0
    for _ in range(h):
        ftype = raw[i]
        line = bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if ftype == 1:
                line[x] = (line[x] + a) & 255
            elif ftype == 2:
                line[x] = (line[x] + b) & 255
            elif ftype == 3:
                line[x] = (line[x] + (a + b) // 2) & 255
            elif ftype == 4:
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                line[x] = (line[x] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        for x in range(0, stride, bpp):
            r, g, bl = line[x], line[x + 1], line[x + 2]
            if r > 200 and g > 170 and bl < 80:
                count += 1
        prev = line
    return count


def test(sim):
    sim.cli("circuit", "new", "--discard")
    r1 = sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "300,300")["pins"]
    q1 = sim.cli("comp", "add", "BJT", "--label", "Q1", "--at", "500,300")["pins"]
    u1 = sim.cli("comp", "add", "opAmp", "--label", "U1", "--at", "700,300")["pins"]

    # Auto pin: topmost, then rightmost
    sim.cli("select", "R1")
    s = sim.cli("kbd", "state")
    sim.check("R1 auto pin = rPin", int(s["selectedPin"] == default_pin(r1) == "R1.rPin"), 1, absolute=0)
    sim.check("R1 auto pin is not manual", int(s["selectedPinManual"]), 0, absolute=0)

    # > and < on R1
    s = sim.cli("ui", "type", ">")
    sim.check("R1 >: lPin, manual", int(s["selectedPin"] == "R1.lPin" and s["selectedPinManual"]), 1, absolute=0)
    sim.check("R1 >: pinTo", int(s["pinTo"] == "R1.lPin"), 1, absolute=0)
    s = sim.cli("ui", "type", "<")
    sim.check("R1 <: back to rPin", int(s["selectedPin"] == "R1.rPin" and s["pinTo"] == "R1.rPin"), 1, absolute=0)

    # BJT: clockwise order from the pin positions. Fresh circuit: no last part, so the
    # selection rule of K4 does not pair Q1's auto pin with another part's pin
    sim.cli("circuit", "new", "--discard")
    q1 = sim.cli("comp", "add", "BJT", "--label", "Q1", "--at", "500,300")["pins"]
    order = clockwise(q1)
    sim.cli("select", "Q1")
    s = sim.cli("kbd", "state")
    sim.check("Q1 pins clockwise", int(s["pinsClockwise"] == order), 1, absolute=0, note=str(s["pinsClockwise"]))
    p = default_pin(q1)
    sim.check("Q1 auto pin (collector)", int(s["selectedPin"] == p), 1, absolute=0, note=s["selectedPin"])
    ok = True
    for _ in range(3):
        p = step(order, p, 1)
        ok &= sim.cli("ui", "type", ">")["selectedPin"] == p
    sim.check("Q1 > walks clockwise and wraps", int(ok), 1, absolute=0)
    p = step(order, p, -1)
    sim.check("Q1 < goes back", int(sim.cli("ui", "type", "<")["selectedPin"] == p), 1, absolute=0)

    # Rotation keeps the pin
    before = sim.cli("kbd", "state")["selectedPin"]
    sim.cli("comp", "rotate", "Q1", "cw")
    sim.cli("select", "Q1")
    sim.check("rotate keeps the selected pin", int(sim.cli("kbd", "state")["selectedPin"] == before), 1, absolute=0)

    # Op amp (power pins hidden): Cmd+> sets pinFrom, then > sets pinTo, pinFrom stays
    sim.cli("circuit", "new", "--discard")
    u1 = sim.cli("comp", "add", "opAmp", "--label", "U1", "--at", "700,300")["pins"]
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "300,300")
    sim.cli("comp", "add", "BJT", "--label", "Q1", "--at", "500,300")
    order = clockwise(u1)
    sim.cli("select", "U1")
    p0 = sim.cli("kbd", "state")["selectedPin"]
    sim.check("U1: hidden power pins left out", len(order), 3, absolute=0)
    s = sim.cli("ui", "type", "cmd+>")
    pf = step(order, p0, 1)
    sim.check("U1 Cmd+>: pinFrom", int(s["pinFrom"] == pf and s["selectedPin"] == pf), 1, absolute=0, note=str(s["pinFrom"]))
    s = sim.cli("ui", "type", ">")
    sim.check("U1 >: pinTo next, pinFrom stays", int(s["pinTo"] == step(order, pf, 1) and s["pinFrom"] == pf), 1, absolute=0)
    s = sim.cli("ui", "type", "cmd+<")
    sim.check("U1 Cmd+<: pinFrom back", int(s["pinFrom"] == pf), 1, absolute=0)

    # Two parts selected: nothing changes
    sim.cli("select", "R1", "Q1")
    s0 = sim.cli("kbd", "state")
    s = sim.cli("ui", "type", ">")
    sim.check("two parts: > does nothing", int(s["pinTo"] == s0["pinTo"]), 1, absolute=0)

    # Colours on the view (Dark theme): a part never selected has no highlight; selected,
    # its pin is highlighted; unselected again, the pin stays highlighted as pinTo (K4)
    sim.cli("settings", "set", "colorTheme", "Dark")
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "0,0")
    sim.cli("view", "center", "R1")
    tmp = tempfile.mkdtemp()
    sim.cli("ui", "focus", "none")                    # hide the cross
    never = yellow_pixels(sim, os.path.join(tmp, "never.png"))
    sim.cli("select", "R1")
    sel = yellow_pixels(sim, os.path.join(tmp, "sel.png"))
    sim.cli("select", "--none")
    unsel = yellow_pixels(sim, os.path.join(tmp, "unsel.png"))
    sim.check("never selected: no highlight", never, 0, absolute=0)
    sim.check("selected: pin highlighted", int(sel > 5), 1, absolute=0, note="%d px" % sel)
    sim.check("unselected: pin kept as pinTo", int(unsel > 5), 1, absolute=0, note="%d px" % unsel)
    sim.cli("settings", "set", "colorTheme", "Classic")

    # Deleting the selected part takes the wiring pair with it: the outgoing pin of
    # the previously connected part is unset too (its ring goes)
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    sim.cli("ui", "type", "r")                    # pinFrom R1.rPin, pinTo R2.lPin
    s = sim.cli("kbd", "state")
    sim.check("the pair is set", int(bool(s["pinFrom"]) and bool(s["pinTo"])), 1, absolute=0,
              note="%s -> %s" % (s["pinFrom"], s["pinTo"]))
    sim.cli("ui", "type", "backspace")            # Delete the selected part (R2)
    s = sim.cli("kbd", "state")
    sim.check("delete: pinFrom unset", int(s["pinFrom"] is None), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("delete: pinTo unset", int(s["pinTo"] is None), 1, absolute=0, note=str(s["pinTo"]))

    # Rotating a selected part with pinFrom on another part moves it so its closest
    # pin shares the outgoing pin's row or column (D19). A BJT after R1: after cmd+r
    # one of its pins sits on R1.rPin's row (y=100); rotating R1 itself (pinFrom is
    # its own pin) is the plain rotation, no move
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    r1 = sim.cli("kbd", "state")["lastPart"]
    sim.cli("ui", "type", "t")
    bjt = sim.cli("kbd", "state")["lastPart"]
    placed = sim.cli("comp", "get", bjt)
    sim.cli("ui", "type", "cmd+r")
    pins = sim.cli("comp", "get", bjt)["pins"]
    sim.check("rotated: a pin on the outgoing pin's row",
              int(any(p["pos"][1] == 100 and p["pos"][0] > 116 for p in pins)), 1, absolute=0,
              note=str([(p["id"], p["pos"]) for p in pins]))
    # The rotation and its alignment shift are one undo step: one cmd+z restores both
    rotated = sim.cli("comp", "get", bjt)
    sim.cli("ui", "type", "cmd+z")
    g = sim.cli("comp", "get", bjt)
    sim.check("rotate+align: one cmd+z restores rotation and place",
              int(g["pos"] == placed["pos"] and g.get("rotation") == placed.get("rotation")), 1, absolute=0,
              note="%s %s" % (g["pos"], g.get("rotation")))
    sim.cli("ui", "type", "cmd+shift+z")
    g = sim.cli("comp", "get", bjt)
    sim.check("rotate+align: redo puts it back rotated and aligned",
              int(g["pos"] == rotated["pos"] and g.get("rotation") == rotated.get("rotation")), 1, absolute=0,
              note="%s %s" % (g["pos"], g.get("rotation")))
    sim.cli("select", bjt)
    pos = sim.cli("comp", "get", r1)["pos"]
    sim.cli("select", r1)
    sim.cli("ui", "type", "cmd+>")                # pinFrom onto R1's own pin
    sim.cli("ui", "type", "cmd+r")
    sim.check("rotating the pinFrom part: not moved",
              int(sim.cli("comp", "get", r1)["pos"] == pos), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
