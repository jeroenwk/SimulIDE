"""Selection rectangle (specs/keyboard-input.md K11, D23): Shift+arrows from the cross.

Expected values follow the spec: nothing selected, Shift+arrows draw a rectangle from the
cross (8 px steps, Cmd+Shift 64 px); every part it touches is selected while it grows
(parts only, not wires or dots); releasing Shift ends it with the parts selected, and the
arrows then move them; Esc cancels it. The arrows move 8 px, Cmd+arrows 64 px: the cross,
or the selection (Shift+arrows: 8 px too, no rectangle). Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def sel(s):
    return sorted(u.split("-")[0] for u in s["selection"])


def setup(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "300,100")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "100,300")
    sim.cli("wire", "add", "R1.rPin", "R2.lPin")
    sim.cli("ui", "click", "52,52")                       # The cross at (52, 52)


def labels(sim, s):
    return sorted(sim.cli("comp", "get", u)["label"] for u in s["selection"])


def test(sim):
    setup(sim)

    # The cross: arrows 8 px, Cmd+arrows 64 px
    s = sim.cli("ui", "type", "Right")
    sim.check("Right: the cross +8", s["cursor"][0], 60, absolute=0)
    s = sim.cli("ui", "type", "cmd+Down")
    sim.check("Cmd+Down: the cross +64", s["cursor"][1], 116, absolute=0)
    s = sim.cli("ui", "type", "Left", "cmd+Up")
    sim.check("back at (52, 52)", int(s["cursor"] == [52, 52]), 1, absolute=0, note=str(s["cursor"]))

    # Shift+arrows: a rectangle from the cross; R1 selected as soon as it touches it
    s = sim.cli("ui", "type", "cmd+shift+Right", "cmd+shift+Right", "cmd+shift+Right", "cmd+shift+Down")
    sim.check("rectangle: drawn from the cross", int(s.get("rect") == [52, 52, 192, 64]), 1, absolute=0,
              note=str(s.get("rect")))
    sim.check("rectangle: R1 selected", int(labels(sim, s) == ["R1"]), 1, absolute=0, note=str(labels(sim, s)))
    sim.check("rectangle: the cross shown (its corner)", int(s["cursorVisible"] and s["cursor"] == [244, 116]), 1,
              absolute=0, note=str(s["cursor"]))
    s = sim.cli("ui", "type", "cmd+shift+Right")
    sim.check("grown: R1 and R2, not the wire", int(labels(sim, s) == ["R1", "R2"] and len(s["selection"]) == 2), 1,
              absolute=0, note=str(s["selection"]))
    s = sim.cli("ui", "type", "shift+Down", "shift+Down")
    sim.check("8 px steps", int(s.get("rect") == [52, 52, 256, 80]), 1, absolute=0, note=str(s.get("rect")))
    s = sim.cli("ui", "type", "cmd+shift+Left", "cmd+shift+Left", "cmd+shift+Left", "cmd+shift+Left")
    sim.check("shrunk past R2 and R1: nothing", int(s["selection"] == []), 1, absolute=0, note=str(s["selection"]))
    s = sim.cli("ui", "type", "cmd+shift+Right", "cmd+shift+Right", "cmd+shift+Right", "cmd+shift+Right")
    sim.check("grown again: R1, R2", int(labels(sim, s) == ["R1", "R2"]), 1, absolute=0)

    # Releasing Shift ends it: the parts stay selected, the arrows move them
    s = sim.cli("ui", "type", "shift")
    sim.check("Shift up: rectangle gone", int("rect" not in s), 1, absolute=0)
    sim.check("Shift up: R1, R2 still selected", int(labels(sim, s) == ["R1", "R2"]), 1, absolute=0)
    sim.check("Shift up: the cross hidden (parts selected)", int(s["cursorVisible"]), 0, absolute=0)
    sim.cli("ui", "type", "cmd+Right")
    sim.check("then Cmd+Right: R1 moved 64", sim.cli("comp", "get", "R1")["pos"][0], 164, absolute=0)
    sim.check("then Cmd+Right: R2 moved 64", sim.cli("comp", "get", "R2")["pos"][0], 364, absolute=0)
    sim.check("R3 stays", sim.cli("comp", "get", "R3")["pos"][0], 100, absolute=0)

    # Esc cancels: nothing selected
    setup(sim)
    s = sim.cli("ui", "type", "cmd+shift+Down", "cmd+shift+Down", "cmd+shift+Down", "cmd+shift+Down",
                "cmd+shift+Right")
    sim.check("R1 and R3 in the rectangle", int(labels(sim, s) == ["R1", "R3"]), 1, absolute=0,
              note=str(labels(sim, s)))
    s = sim.cli("ui", "type", "Esc")
    sim.check("Esc: cancelled, nothing selected", int("rect" not in s and s["selection"] == []), 1, absolute=0)
    sim.check("Esc: the cross at the corner", int(s["cursorVisible"] and s["cursor"] == [116, 308]), 1, absolute=0,
              note=str(s["cursor"]))

    # A click ends it too, keeping the parts
    setup(sim)
    sim.cli("ui", "type", "cmd+shift+Right", "cmd+shift+Down")
    s = sim.cli("ui", "click", "500,500")
    sim.check("a click ends it", int("rect" not in s), 1, absolute=0)

    # A part selected: the arrows move it 8 px, Cmd 64 (D23); Shift+arrows too, no rectangle
    setup(sim)
    sim.cli("select", "R3")
    s = sim.cli("ui", "type", "Right")
    sim.check("part selected: Right moves it 8", sim.cli("comp", "get", "R3")["pos"][0], 108, absolute=0)
    s = sim.cli("ui", "type", "cmd+Right")
    sim.check("part selected: Cmd+Right moves it 64", sim.cli("comp", "get", "R3")["pos"][0], 172, absolute=0)
    s = sim.cli("ui", "type", "shift+Right")
    sim.check("part selected: Shift+Right moves it 8", sim.cli("comp", "get", "R3")["pos"][0], 180, absolute=0)
    sim.check("part selected: no rectangle", int("rect" not in s), 1, absolute=0)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: Shift+Arrows rectangle",
              int(any("rectangle" in r["action"] and r["keys"].startswith("Shift+Arrows") for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
