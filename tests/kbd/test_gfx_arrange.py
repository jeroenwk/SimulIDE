"""Graphical shapes and Space (specs/gfx-arrange.md).

Expected values follow the spec:
- Space with nothing selected selects the part or wire nearest to the cross (a wire:
  network mode on it); inside a background shape the parts drawn on it come first.
- Cmd+Shift+F / Opt+Cmd+Shift+F / Opt+Cmd+Shift+B / Cmd+Shift+B bring to front, forward,
  send backward, to back: the Z values of the shapes are reordered among the values they
  already use (ties pushed down), one step passes the next shape it overlaps; one undo step.
- = on a single shape highlights the corner nearest to the cross (or the click); Tab /
  Shift+Tab go round clockwise; arrows move it 8 px (Cmd 64), the opposite corner stays;
  a burst of arrows is one undo step; Esc leaves the corner, the shape stays selected.
- A drag of a handle moves that corner (4 px grid steps); a Line has two handles (its
  ends) and flips when one end is moved past the other.
Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def z(sim, comp):
    for p in sim.cli("comp", "get", comp, "--all")["properties"]:
        if p["name"] == "Z_Value":
            return float(p["value"])
    raise AssertionError("no Z_Value on " + comp)


def handles(s):
    return {h["corner"]: tuple(h["at"]) for h in s["shape"]["handles"]}


def ok(sim, name, cond, note=""):
    sim.check(name, int(bool(cond)), 1, absolute=0, note=str(note))


def test(sim):
    sim.cli("circuit", "new", "--discard")

    # ---- Space: the nearest part or wire ------------------------------------------
    r1 = sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")["uid"]
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "300,100")
    sim.cli("wire", "add", "R1.rPin", "R2.lPin")
    sim.cli("ui", "click", "196,108")                  # Under the wire's middle
    s = sim.cli("ui", "type", "space")
    ok(sim, "space near a wire: network mode on it", s.get("network") and s["network"]["step"]["kind"] == "wire", s.get("network"))
    sim.cli("ui", "type", "esc")
    sim.cli("ui", "click", "100,140")                  # Below R1
    s = sim.cli("ui", "type", "space")
    ok(sim, "space near R1: R1 selected", s["selection"] == [r1], s["selection"])

    # A background rectangle round R1: the cross near R1 selects R1, near its edge the rectangle
    bg = sim.cli("comp", "add", "Rectangle", "--label", "BG", "--at", "100,100", "H_size=200", "V_size=160")["uid"]
    sim.cli("ui", "click", "100,196")                  # Below it (a click inside would select it),
    s = sim.cli("ui", "type", *(["Up"] * 7))           # then the cross to 100,140
    ok(sim, "cross inside the shape, nothing selected", s["cursor"] == [100, 140] and not s["selection"], s)
    s = sim.cli("ui", "type", "space")
    ok(sim, "space inside a shape, near R1: R1", s["selection"] == [r1], s["selection"])
    sim.cli("ui", "click", "20,196")
    sim.cli("ui", "type", *(["Up"] * 4))               # 20,164: 16 px inside its bottom edge
    s = sim.cli("ui", "type", "space")
    ok(sim, "space near the shape's edge: the shape", s["selection"] == [bg], s["selection"])
    sim.cli("comp", "remove", "BG", "R1", "R2")

    # ---- Stacking order -----------------------------------------------------------
    for name, x in (("A", 200), ("B", 220), ("C", 240), ("D", 600)):   # D overlaps none
        sim.cli("comp", "add", "Rectangle", "--label", name, "--at", "%d,200" % x)
    zs = lambda: [z(sim, n) for n in "ABCD"]
    ok(sim, "shapes start at Z -1", zs() == [-1, -1, -1, -1], zs())

    sim.cli("select", "A")
    sim.cli("ui", "type", "cmd+shift+f")
    order = sorted("ABCD", key=lambda n: z(sim, n))
    ok(sim, "bring to front: A on top", order == list("BCDA"), (order, zs()))
    ok(sim, "the Z values stay in their range (ties pushed down)", sorted(zs()) == [-4, -3, -2, -1], zs())

    sim.cli("ui", "type", "opt+cmd+shift+b")           # Past D (no overlap: past the next one)
    order = sorted("ABCD", key=lambda n: z(sim, n))
    ok(sim, "send backward: past the next shape it overlaps (C, not D)", order == list("BACD"), (order, zs()))

    before = zs()
    sim.cli("ui", "type", "cmd+shift+b")
    order = sorted("ABCD", key=lambda n: z(sim, n))
    ok(sim, "send to back", order == list("ABCD"), (order, zs()))
    sim.cli("undo")
    ok(sim, "undo: one step", zs() == before, (zs(), before))
    sim.cli("redo")

    sim.cli("select", "C")
    sim.cli("ui", "type", "opt+cmd+shift+f")           # Nothing above overlaps C: past D
    order = sorted("ABCD", key=lambda n: z(sim, n))
    ok(sim, "bring forward without overlap: past the next one", order == list("ABDC"), (order, zs()))
    sim.cli("ui", "type", "opt+cmd+shift+f")
    m = sim.cli("messages", "--last", "1")["lines"]
    ok(sim, "already in front: a message", m and "already in front" in m[-1], m)

    sim.cli("select", "--none")
    sim.cli("ui", "type", "cmd+shift+f")
    m = sim.cli("messages", "--last", "1")["lines"]
    ok(sim, "no shape selected: a message", m and "no shape selected" in m[-1], m)
    for n in "ABCD":
        sim.cli("comp", "remove", n)

    # ---- Corners: keys --------------------------------------------------------------
    su = sim.cli("comp", "add", "Rectangle", "--label", "S", "--at", "200,200")["uid"]   # 50 x 30
    sim.cli("ui", "click", "236,220")                  # Near its bottom right corner
    sim.cli("select", "S")
    s = sim.cli("kbd", "state")
    h = handles(s)
    ok(sim, "four handles at the corners", h == {0: (175, 185), 1: (225, 185), 2: (225, 215), 3: (175, 215)}, h)
    ok(sim, "no corner highlighted before =", "corner" not in s["shape"], s["shape"])
    s = sim.cli("ui", "type", "=")
    ok(sim, "=: the corner nearest the click (bottom right)", s["shape"].get("corner") == 2, s["shape"])
    s = sim.cli("ui", "type", "tab")
    ok(sim, "tab: clockwise (bottom left)", s["shape"].get("corner") == 3, s["shape"])
    s = sim.cli("ui", "type", "tab")
    ok(sim, "tab wraps (top left)", s["shape"].get("corner") == 0, s["shape"])
    s = sim.cli("ui", "type", "shift+tab", "shift+tab")
    ok(sim, "shift+tab: back (bottom right)", s["shape"].get("corner") == 2, s["shape"])

    s = sim.cli("ui", "type", "Right", "Right", "Down")
    h = handles(s)
    ok(sim, "arrows move the corner 8 px", h[2] == (241, 223), h)
    ok(sim, "the opposite corner stays", h[0] == (175, 185), h)
    ok(sim, "size follows", s["shape"]["size"] == [66, 38], s["shape"]["size"])
    s = sim.cli("ui", "type", "cmd+Left")
    ok(sim, "cmd+arrow: 64 px", s["shape"]["size"][0] == 2 and handles(s)[0] == (175, 185), s["shape"])
    sim.cli("undo")
    s = sim.cli("kbd", "state")
    ok(sim, "undo: the arrows were one step", s["shape"]["size"] == [50, 30] and handles(s)[2] == (225, 215), s["shape"])
    s = sim.cli("ui", "type", "esc")
    ok(sim, "esc: the corner goes, the shape stays selected", s["selection"] == [su] and "corner" not in s["shape"], s)

    # ---- Corners: mouse ---------------------------------------------------------------
    s = sim.cli("ui", "drag", "175,185", "159,169")
    h = handles(s)
    ok(sim, "drag of the top left handle", h[0] == (159, 169) and h[2] == (225, 215), h)
    ok(sim, "the dragged corner is highlighted", s["shape"].get("corner") == 0, s["shape"])
    ok(sim, "still selected, not moved as a whole", s["selection"] == [su], s["selection"])
    sim.cli("undo")
    s = sim.cli("kbd", "state")
    ok(sim, "undo: the drag was one step", handles(s)[0] == (175, 185), handles(s))
    sim.cli("comp", "remove", "S")

    # ---- A Line: two ends, flips --------------------------------------------------------
    sim.cli("comp", "add", "Line", "--label", "L", "--at", "400,200")
    sim.cli("ui", "click", "380,212")
    sim.cli("select", "L")
    s = sim.cli("ui", "type", "=")
    h = handles(s)
    ok(sim, "line: two handles, its ends", h == {1: (425, 185), 3: (375, 215)}, h)
    ok(sim, "line: = takes the end nearest the click", s["shape"].get("corner") == 3, s["shape"])
    s = sim.cli("ui", "type", *(["Right"] * 7))       # 56 px: past the other end (x 425)
    h = handles(s)
    ok(sim, "line: the end moved past the other one", h[3] == (431, 215) and h[1] == (425, 185), h)
    ok(sim, "line: flipped instead of shrinking", s["shape"]["flip"][0] == -1 and s["shape"]["size"] == [6, 30], s["shape"])

    # ---- Registry -------------------------------------------------------------------------
    sc = sim.cli("kbd", "shortcuts")
    ok(sim, "registry: Space selects the nearest", any(x["keys"] == "Space" and "nearest" in x["action"] for x in sc))
    ok(sim, "registry: the four arrange keys", sum(1 for x in sc if x.get("category") == "Shapes" and "Cmd+Shift" in x["keys"]) == 4)


if __name__ == "__main__":
    run_test(test)
