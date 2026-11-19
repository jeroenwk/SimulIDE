"""K9 (specs/keyboard-input.md): network mode, walking wires / dots / corners.

Expected values follow the spec: = with one part selected whose selected pin has a
wire enters network mode on that wire; Tab / Shift+Tab walk every wire, corner and
dot reachable from the pin (never through a part), nearest along the wires first,
a dot before the wires leaving it, wrapping; arrows move a dot (wires follow) or a
corner (its wire stays horizontal / vertical), not a wire; Backspace deletes a wire
(one undo step), not a dot or corner; = or Esc goes back to parts mode with the part
selected and its pin unchanged; a pin without a wire gives a message and stays in
parts mode. Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def net(s):
    return s.get("network")


def step(s):
    return s["network"]["step"]


def wires(sim):
    return {w["uid"]: w for w in sim.cli("wire", "list")}


def hv(points):
    """Every segment horizontal or vertical."""
    p = list(zip(points[0::2], points[1::2]))
    return all(a[0] == b[0] or a[1] == b[1] for a, b in zip(p, p[1:]))


def build(sim):
    """R1 and R2 joined by a dot on R1's wire to R3 (the spec's validation circuit)."""
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "100,200")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "300,148")
    sim.cli("wire", "add", "R1.rPin", "R3.lPin")
    sim.cli("wire", "add", "R2.rPin", "R1.rPin")       # A dot on R1's wire at 200,100


def test(sim):
    build(sim)
    sim.cli("select", "R1")
    before = sim.cli("kbd", "state")["selectedPin"]
    s = sim.cli("ui", "type", "=")
    sim.check("=: network mode", int(net(s) is not None), 1, absolute=0)
    first = step(s)
    sim.check("=: first step is R1's wire", int(first["kind"] == "wire" and first["len"] == 0), 1,
              absolute=0, note=str(first))
    sim.check("=: that wire highlighted", int(net(s)["selectedWires"] == [first["uid"]]), 1, absolute=0,
              note=str(net(s)["selectedWires"]))
    sim.check("=: the cross is hidden", int(s["cursorVisible"]), 0, absolute=0)
    walk = net(s)["walk"]
    sim.check("walk: 3 wires, 1 dot, 2 corners", int(sorted(w.split()[0] for w in walk)
              == ["corner", "corner", "dot", "wire", "wire", "wire"]), 1, absolute=0, note=str(walk))
    sim.check("walk: the dot comes right after the first wire", int(walk[1].startswith("dot")), 1,
              absolute=0, note=str(walk))

    # Tab: nearest first (lengths never decrease), wraps; Shift+Tab goes back
    lens = [first["len"]]
    for _ in range(len(walk) - 1):
        lens.append(step(sim.cli("ui", "type", "tab"))["len"])
    sim.check("tab: nearest first", int(lens == sorted(lens)), 1, absolute=0, note=str(lens))
    s = sim.cli("ui", "type", "tab")
    sim.check("tab: wraps to the first", int(step(s) == first), 1, absolute=0, note=str(step(s)))
    s = sim.cli("ui", "type", "shift+tab")
    sim.check("shift+tab: back to the last", int(step(s)["len"] == lens[-1]), 1, absolute=0, note=str(step(s)))

    # A wire: arrows do nothing
    s = sim.cli("ui", "type", "tab")                     # The first wire again
    w0 = wires(sim)
    sim.cli("ui", "type", "Right", "shift+Down")
    sim.check("wire: arrows do nothing", int(wires(sim) == w0), 1, absolute=0)

    # The dot: Shift+Right moves it 8 px, its wires follow; Backspace does nothing
    s = sim.cli("ui", "type", "tab")
    sim.check("tab: the dot", int(step(s)["kind"] == "dot"), 1, absolute=0, note=str(step(s)))
    sim.check("dot: highlighted", int(net(s)["selectedDots"] == [step(s)["uid"]]), 1, absolute=0)
    dot = step(s)["pos"]
    s = sim.cli("ui", "type", "shift+Right")
    sim.check("dot: moved 8 px", int(step(s)["kind"] == "dot" and step(s)["pos"] == [dot[0] + 8, dot[1]]), 1,
              absolute=0, note=str(step(s)))
    ends = [p for w in wires(sim).values() for p in zip(w["points"][0::2], w["points"][1::2])]
    sim.check("dot: its wires follow", int((dot[0] + 8, dot[1]) in ends and (dot[0], dot[1]) not in ends), 1,
              absolute=0)
    n = len(wires(sim))
    sim.cli("ui", "type", "Backspace")
    sim.check("dot: Backspace does nothing", len(wires(sim)), n, absolute=0)

    # A corner: arrows move it, its wire stays H/V; two presses are one undo step;
    # Backspace does nothing
    s = sim.cli("ui", "type", "tab")
    while step(s)["kind"] != "corner":
        s = sim.cli("ui", "type", "tab")
    c = step(s)
    old = wires(sim)[c["uid"]]["points"]
    s = sim.cli("ui", "type", "Up", "shift+Up")
    sim.check("corner: moved 72 px up", int(step(s)["kind"] == "corner" and step(s)["pos"] == [c["pos"][0], c["pos"][1] - 72]),
              1, absolute=0, note=str(step(s)))
    new = wires(sim)[c["uid"]]["points"]
    sim.check("corner: its wire stays H/V", int(hv(new)), 1, absolute=0, note=str(new))
    n = len(wires(sim))
    sim.cli("ui", "type", "Backspace")
    sim.check("corner: Backspace does nothing", len(wires(sim)), n, absolute=0)
    s = sim.cli("ui", "type", "cmd+z")                    # Any other key ends network mode first
    sim.check("cmd+z: back to parts mode", int(net(s) is None), 1, absolute=0)
    sim.check("cmd+z: the two corner moves undone at once", int(wires(sim)[c["uid"]]["points"] == old), 1,
              absolute=0, note=str(wires(sim)[c["uid"]]["points"]))

    # = and Esc: back to parts mode, R1 selected, its pin unchanged
    sim.cli("select", "R1")
    sim.cli("ui", "type", "=", "tab", "tab")
    s = sim.cli("ui", "type", "=")
    sim.check("=: parts mode again", int(net(s) is None), 1, absolute=0)
    sim.check("=: R1 selected", int(s["selection"] == ["Resistor-1"]), 1, absolute=0, note=str(s["selection"]))
    sim.check("=: its pin unchanged", int(s["selectedPin"] == before), 1, absolute=0, note=str(s["selectedPin"]))
    sim.cli("ui", "type", "=")
    s = sim.cli("ui", "type", "esc")
    sim.check("esc: parts mode, R1 selected", int(net(s) is None and s["selection"] == ["Resistor-1"]), 1,
              absolute=0, note=str(s["selection"]))

    # A wire: Backspace deletes it (one undo step); the walk goes on with the nearest step
    build(sim)
    sim.cli("select", "R3")
    sim.cli("ui", "type", "<")                            # R3.lPin: its wire to the dot
    s = sim.cli("ui", "type", "=")
    sim.check("delete setup: walking from R3.lPin", int(net(s) is not None and net(s)["pin"].endswith("lPin")), 1,
              absolute=0, note=str(net(s)))
    n = len(wires(sim))
    s = sim.cli("ui", "type", "Backspace")
    sim.check("delete: the wire is gone (the dot dissolves)", int(len(wires(sim)) < n), 1, absolute=0,
              note="%d -> %d" % (n, len(wires(sim))))
    sim.check("delete: R3's pin has no wire left: parts mode", int(net(s) is None), 1, absolute=0)
    sim.check("delete: R3 selected again", int(s["selection"] == ["Resistor-3"]), 1, absolute=0,
              note=str(s["selection"]))
    sim.cli("ui", "type", "cmd+z")
    sim.check("delete: cmd+z restores the wire", len(wires(sim)), n, absolute=0)

    build(sim)
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "=", "tab", "tab")          # The wire to R2 or to R3 (from the dot)
    gone = step(s)["uid"]
    s = sim.cli("ui", "type", "Backspace")
    sim.check("delete from the dot: still network mode", int(net(s) is not None), 1, absolute=0)
    sim.check("delete from the dot: the wire is gone", int(gone not in wires(sim)), 1, absolute=0)
    sim.check("delete from the dot: a wire is selected next", int(step(s)["kind"] == "wire"), 1,
              absolute=0, note=str(step(s)))

    # A pin without a wire: a message, still parts mode
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "=")
    sim.check("no wire: parts mode", int(net(s) is None), 1, absolute=0)
    sim.check("no wire: message", int("no wire" in str(sim.cli("messages"))), 1, absolute=0)
    sim.check("no wire: R1 still selected", int(s["selection"] == ["Resistor-1"]), 1, absolute=0)

    # A mouse click ends network mode; h pans without leaving it
    build(sim)
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "=", "h", "Right")
    sim.check("h: pan keeps the walk", int(net(s) is not None and s["panMode"]), 1, absolute=0)
    s = sim.cli("ui", "click", "500,500")
    sim.check("click: parts mode", int(net(s) is None), 1, absolute=0)

    # = with nothing selected (K9c): from the dot, corner or wire nearest to the cross;
    # a dot or corner next to the cross beats the wire it is on
    build(sim)                                            # Dot 200,100; corners 284,100 and 200,200
    sim.cli("ui", "click", "204,108")
    s = sim.cli("ui", "type", "=")
    sim.check("nothing selected, near the dot: the dot", int(net(s) is not None and step(s)["kind"] == "dot"), 1,
              absolute=0, note=str(net(s) and step(s)))
    sim.check("nothing selected: no pin", int(net(s)["pin"] == ""), 1, absolute=0, note=str(net(s)["pin"]))
    sim.check("nothing selected: the whole net (6 steps)", net(s)["steps"], 6, absolute=0)
    s = sim.cli("ui", "type", "esc")
    sim.check("esc: parts mode, nothing selected", int(net(s) is None and s["selection"] == []), 1, absolute=0)
    sim.check("esc: the cross shown at the dot", int(s["cursorVisible"] and abs(s["cursor"][0] - 200) <= 4
              and abs(s["cursor"][1] - 100) <= 4), 1, absolute=0, note=str(s["cursor"]))

    sim.cli("ui", "click", "292,108")
    s = sim.cli("ui", "type", "=")
    sim.check("near a corner: the corner", int(step(s)["kind"] == "corner" and step(s)["pos"] == [284, 100]), 1,
              absolute=0, note=str(step(s)))
    sim.cli("ui", "type", "esc")
    sim.cli("ui", "click", "244,108")
    s = sim.cli("ui", "type", "=")
    sim.check("near the middle of a wire: the wire", int(step(s)["kind"] == "wire"), 1, absolute=0,
              note=str(step(s)))
    sim.check("near a wire: highlighted", int(net(s)["selectedWires"] == [step(s)["uid"]]), 1, absolute=0)
    lens = [step(sim.cli("ui", "type", "tab"))["kind"] for _ in range(6)]
    sim.check("tab from a wire: walks the net and wraps", int(sorted(lens) == ["corner", "corner", "dot",
              "wire", "wire", "wire"]), 1, absolute=0, note=str(lens))

    # Deleting the wire it started from: the walk goes on from the nearest item left
    sim.cli("ui", "type", "esc")
    sim.cli("ui", "click", "244,108")
    s = sim.cli("ui", "type", "=", "Backspace")
    sim.check("delete the start wire: still network mode", int(net(s) is not None), 1, absolute=0)
    sim.check("delete the start wire: a wire selected", int(step(s)["kind"] == "wire"), 1, absolute=0,
              note=str(step(s)))
    sim.cli("ui", "type", "esc")

    # Cmd+click (K9d) on a wire, a corner or a dot: network mode with that item selected
    # and highlighted, as the keyboard selects it; the keys go on from there
    build(sim)
    w0 = wires(sim)
    s = sim.cli("ui", "click", "240,100", "--cmd")
    sim.check("cmd+click a wire: network mode on it", int(net(s) is not None and step(s)["kind"] == "wire"), 1,
              absolute=0, note=str(net(s) and step(s)))
    sim.check("cmd+click a wire: the whole wire highlighted", int(net(s)["selectedWires"] == [step(s)["uid"]]), 1,
              absolute=0, note=str(net(s)["selectedWires"]))
    sim.check("cmd+click a wire: the canvas has the keys", int(s["focused"]), 1, absolute=0)
    sim.cli("ui", "click", "240,100", "--cmd")           # Again, on the selected wire: it must not move
    sim.check("cmd+click: the wire does not move (no scroll under the button)", int(wires(sim) == w0), 1,
              absolute=0, note=str([w["points"] for w in wires(sim).values()]))
    s = sim.cli("ui", "click", "282,100", "--cmd")    # On the line, 2 px before the corner
    sim.check("cmd+click near a corner: the corner", int(step(s)["kind"] == "corner" and step(s)["pos"] == [284, 100]), 1,
              absolute=0, note=str(step(s)))
    s = sim.cli("ui", "type", "shift+Up")
    sim.check("cmd+click a corner, then arrows: it moves", int(step(s)["pos"] == [284, 92]), 1, absolute=0,
              note=str(step(s)))
    sim.cli("ui", "type", "cmd+z")
    s = sim.cli("ui", "click", "200,100", "--cmd")
    sim.check("cmd+click the dot: the dot", int(step(s)["kind"] == "dot"), 1, absolute=0, note=str(step(s)))
    sim.check("cmd+click the dot: highlighted", int(net(s)["selectedDots"] == [step(s)["uid"]]), 1, absolute=0)
    s = sim.cli("ui", "type", "esc")
    s = sim.cli("ui", "click", "100,100", "--cmd")       # On R1: a part is still toggled
    sim.check("cmd+click a part: selected, no network mode", int(net(s) is None and s["selection"] == ["Resistor-1"]), 1,
              absolute=0, note=str(s["selection"]))
    s = sim.cli("ui", "click", "100,100", "--cmd")
    sim.check("cmd+click a part again: unselected", int(s["selection"] == []), 1, absolute=0, note=str(s["selection"]))

    # No wire in the circuit: a message, parts mode
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("ui", "click", "300,300")
    s = sim.cli("ui", "type", "=")
    sim.check("no wires: parts mode", int(net(s) is None), 1, absolute=0)
    sim.check("no wires: message", int("no wire in the circuit" in str(sim.cli("messages"))), 1, absolute=0)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: = network mode", int(any(r["keys"] == "=" and "Network" in r["action"] for r in sc)), 1,
              absolute=0)


if __name__ == "__main__":
    run_test(test)
