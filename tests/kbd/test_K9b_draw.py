"""K9b (specs/keyboard-input.md): drawing a new wire from a wire with the keyboard.

Expected values follow the spec: + with the cross within 8 px of a wire (nothing
selected) adds a dot on that wire and starts a new wire there; the arrows move the
cross and the wire follows horizontally / vertically; = fixes a corner at the cross,
Backspace removes the last fixed corner; Enter connects to a free pin or a wire within
8 px of the cross (a dot there), or says nothing is there and keeps drawing; the whole
wire, its dot(s) and the split are one undo step; Esc or a mouse press cancels and
leaves the circuit as before. Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def wires(sim):
    return sim.cli("wire", "list")


def hv(points):
    p = list(zip(points[0::2], points[1::2]))
    return all(a[0] == b[0] or a[1] == b[1] for a, b in zip(p, p[1:]))


def build(sim):
    """R1 to R3 by a straight wire on y=100; R2 below, its lPin free at 284,228."""
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "300,100")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "300,228")
    sim.cli("wire", "add", "R1.rPin", "R3.lPin")
    sim.cli("ui", "click", "196,108")                     # The cross 8 px under the wire


def test(sim):
    build(sim)
    before = wires(sim)
    s = sim.cli("ui", "type", "+")
    d = s.get("drawing")
    sim.check("+: drawing", int(d is not None), 1, absolute=0)
    sim.check("+: starts on a dot on the wire", int(d is not None and d["fixed"] == [[196, 100]]), 1, absolute=0,
              note=str(d))
    sim.check("+: the wire is split by the dot (2 + the one drawn)", len(wires(sim)), 3, absolute=0)
    sim.check("+: the cross is shown at the dot", int(s["cursorVisible"] and s["cursor"] == [196, 100]), 1,
              absolute=0, note=str(s["cursor"]))

    # The arrows (Cmd: 64 px, plain: 8 px, D23) trace the wire: it leaves the horizontal wire vertically, then turns
    s = sim.cli("ui", "type", "cmd+Down", "cmd+Down", "cmd+Right", "Right", "Right", "Right")
    pts = s["drawing"]["points"]
    sim.check("trace: the cross at R2.lPin", int(s["cursor"] == [284, 228]), 1, absolute=0, note=str(s["cursor"]))
    sim.check("trace: H/V, down first", int(hv(pts) and pts[:6] == [196, 100, 196, 228, 284, 228]), 1,
              absolute=0, note=str(pts))

    # Enter connects to the free pin: one new wire from the dot to R2.lPin
    s = sim.cli("ui", "type", "enter")
    sim.check("enter: drawing done", int("drawing" not in s), 1, absolute=0)
    ws = wires(sim)
    new = [w for w in ws if w["to"] == "R2.lPin" or w["from"] == "R2.lPin"]
    sim.check("enter: wired to R2.lPin", len(new), 1, absolute=0, note=str(ws))
    sim.check("enter: the new wire is H/V", int(bool(new) and hv(new[0]["points"])), 1, absolute=0,
              note=str(new[0]["points"] if new else None))
    sim.check("enter: 3 wires (split + new)", len(ws), 3, absolute=0)

    # One undo step takes the wire, the dot and the split away
    sim.cli("ui", "type", "cmd+z")
    sim.check("cmd+z: back to the one wire", int(wires(sim) == before), 1, absolute=0, note=str(wires(sim)))
    sim.cli("ui", "type", "cmd+shift+z")
    sim.check("redo: the drawn wire is back", int(any("R2.lPin" in (w["from"], w["to"]) for w in wires(sim))), 1,
              absolute=0)
    redone = [w for w in wires(sim) if "R2.lPin" in (w["from"], w["to"])]
    sim.check("redo: with its route", int(bool(redone) and hv(redone[0]["points"])), 1, absolute=0,
              note=str(redone[0]["points"] if redone else None))

    # = fixes a corner; Backspace removes it again
    build(sim)
    sim.cli("ui", "type", "+", "cmd+Down", "cmd+Right")
    s = sim.cli("ui", "type", "=")
    sim.check("=: the corner fixed", int(s["drawing"]["fixed"] == [[196, 100], [196, 164], [260, 164]]), 1,
              absolute=0, note=str(s["drawing"]["fixed"]))
    s = sim.cli("ui", "type", "cmd+Down", "cmd+Down")
    pts = s["drawing"]["points"]
    sim.check("=: the route goes through it", int(pts[:6] == [196, 100, 196, 164, 260, 164] and hv(pts)), 1,
              absolute=0, note=str(pts))
    s = sim.cli("ui", "type", "Backspace")
    sim.check("backspace: the corner removed", int(s["drawing"]["fixed"] == [[196, 100]]), 1, absolute=0,
              note=str(s["drawing"]["fixed"]))

    # Enter with nothing near: a message, still drawing
    s = sim.cli("ui", "type", "cmd+Left", "cmd+Left", "cmd+Left", "cmd+Left", "enter")
    sim.check("enter, nothing near: still drawing", int("drawing" in s), 1, absolute=0)
    sim.check("enter, nothing near: message", int("nothing to connect" in str(sim.cli("messages"))), 1, absolute=0)

    # Esc cancels: the wire, the dot and the split are gone
    s = sim.cli("ui", "type", "esc")
    sim.check("esc: drawing cancelled", int("drawing" not in s), 1, absolute=0)
    sim.check("esc: the circuit as before", len(wires(sim)), 1, absolute=0, note=str(wires(sim)))

    # Ending on a wire: a dot there. From the R1-R3 wire down to a second wire R4-R5
    build(sim)
    sim.cli("comp", "add", "Resistor", "--label", "R4", "--at", "100,356")
    sim.cli("comp", "add", "Resistor", "--label", "R5", "--at", "300,356")
    sim.cli("wire", "add", "R4.rPin", "R5.lPin")
    sim.cli("ui", "click", "196,108")
    s = sim.cli("ui", "type", "+", "cmd+Down", "cmd+Down", "cmd+Down", "cmd+Down", "enter")
    sim.check("wire end: done", int("drawing" not in s), 1, absolute=0)
    ws = wires(sim)
    sim.check("wire end: both wires split by a dot (5 wires)", len(ws), 5, absolute=0, note=str(ws))
    sim.check("wire end: the new wire is a straight drop",
              int(any(w["points"] == [196, 100, 196, 356] or w["points"] == [196, 356, 196, 100] for w in ws)), 1,
              absolute=0, note=str([w["points"] for w in ws]))

    # A mouse press cancels too
    build(sim)
    sim.cli("ui", "type", "+", "cmd+Down")
    s = sim.cli("ui", "click", "500,500")
    sim.check("click: drawing cancelled", int("drawing" not in s), 1, absolute=0)
    sim.check("click: the circuit as before", len(wires(sim)), 1, absolute=0)

    # + away from wires, or near a wire's end at a pin: a message, nothing drawn
    build(sim)
    sim.cli("ui", "click", "196,148")
    s = sim.cli("ui", "type", "+")
    sim.check("+ off the wire: not drawing", int("drawing" not in s), 1, absolute=0)
    sim.check("+ off the wire: message", int("no wire under the cross" in str(sim.cli("messages"))), 1, absolute=0)
    sim.cli("ui", "click", "116,108")                     # Under R1.rPin, the wire's end
    s = sim.cli("ui", "type", "+")
    sim.check("+ at a pin end: not drawing", int("drawing" not in s), 1, absolute=0)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: + draws a wire", int(any(r["keys"] == "+" for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
