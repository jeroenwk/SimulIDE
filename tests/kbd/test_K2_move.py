"""K2 (specs/keyboard-input.md): moving the selection with the arrow keys.

Expected values follow the spec: with a selection, Arrows move it 64 px and Shift+Arrows 8 px,
the wires attached follow (their end stays on the pin), the keyboard cursor does not move
(K1 rev); a burst of arrow presses on the same selection is one undo step, anything else in
between (another edit, an undo) starts a new one; with nothing selected the arrows don't move
parts. Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def pos(sim, comp):
    return sim.cli("comp", "get", comp)["pos"]


def wire(sim, a, b):
    """Points of the wire between pin refs a and b (either direction)."""
    for w in sim.cli("wire", "list"):
        if {w["from"], w["to"]} == {a, b}:
            return w["points"]
    return None


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "300,300")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "500,300")
    sim.cli("wire", "add", "R1.rPin", "R2.lPin")
    sim.cli("ui", "click", "196,292")                 # empty canvas: cross at (196, 292)

    # Nothing selected: the arrows move the cross (K1 rev), parts stay
    sim.cli("select", "--none")
    s = sim.cli("ui", "type", "Right")
    sim.check("no selection: R1 stays", pos(sim, "R1")[0], 300, absolute=0)
    sim.check("no selection: the cross moves", s["cursor"][0], 260, absolute=0)

    # One part: 64 px, Shift 8 px, wire follows, the cross does not follow (K1 rev)
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "Right")
    sim.check("Right: R1 x", pos(sim, "R1")[0], 364, absolute=0)
    sim.check("Right: the cross does not follow", s["cursor"][0], 260, absolute=0)   # left at 260 by the first Right
    s = sim.cli("ui", "type", "shift+Right", "shift+Down")
    p = pos(sim, "R1")
    sim.check("Shift+Right: R1 x", p[0], 372, absolute=0)
    sim.check("Shift+Down: R1 y", p[1], 308, absolute=0)
    sim.check("Shift moves: the cross does not follow", s["cursor"][1], 292, absolute=0)
    pts = wire(sim, "R1.rPin", "R2.lPin")
    sim.check("wire still on R1.rPin: x", pts[0] if pts else None, 388, absolute=0)
    sim.check("wire still on R1.rPin: y", pts[1] if pts else None, 308, absolute=0)

    # The burst (Right, Shift+Right, Shift+Down, Left x2) is one undo step
    sim.cli("ui", "type", "Left", "Left")
    sim.cli("ui", "type", "cmd+z")
    p = pos(sim, "R1")
    sim.check("Cmd+Z undoes the whole burst: x", p[0], 300, absolute=0)
    sim.check("Cmd+Z undoes the whole burst: y", p[1], 300, absolute=0)

    # Another edit in between starts a new undo step
    sim.cli("select", "R1")
    sim.cli("ui", "type", "Right")                   # A: 300 -> 364
    sim.cli("comp", "move", "R1", "400,300")          # CLI edit
    sim.cli("select", "R1")
    sim.cli("ui", "type", "Right")                   # B: 400 -> 464
    sim.check("after CLI move + Right", pos(sim, "R1")[0], 464, absolute=0)
    sim.cli("ui", "type", "cmd+z")
    sim.check("undo B only", pos(sim, "R1")[0], 400, absolute=0)
    sim.cli("ui", "type", "cmd+z")
    sim.check("undo the CLI move", pos(sim, "R1")[0], 364, absolute=0)

    # Several parts move together
    sim.cli("select", "R1", "R2")
    r2 = pos(sim, "R2")[0]
    sim.cli("ui", "type", "Down")
    sim.check("two parts: R1 y", pos(sim, "R1")[1], 364, absolute=0)
    sim.check("two parts: R2 y", pos(sim, "R2")[1], 364, absolute=0)
    sim.check("two parts: R2 x unchanged", pos(sim, "R2")[0], r2, absolute=0)

    # A junction dot moves with the arrows, its wires follow
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "700,600")
    w = sim.cli("wire", "add", "R1.rPin", "R3.lPin")   # R1.rPin has a wire: a dot is inserted
    node = w["from"].split(".")[0]
    sim.check("a dot was inserted", int(node.startswith("Node")), 1, absolute=0)
    before = [x for x in sim.cli("wire", "list") if x["from"].startswith(node + ".") or x["to"].startswith(node + ".")]
    sim.cli("select", "--none")
    sim.cli("select", node)
    sim.cli("ui", "type", "shift+Down")
    after = [x for x in sim.cli("wire", "list") if x["from"].startswith(node + ".") or x["to"].startswith(node + ".")]
    moved = sum(1 for a, b in zip(sorted(before, key=lambda x: x["uid"]), sorted(after, key=lambda x: x["uid"]))
                if a["points"] != b["points"])
    sim.check("dot moved: its wires changed", moved, len(before), absolute=0)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: Arrows move the selection",
              int(any(r["keys"] == "Arrows" and r["context"] == "Selection" for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
