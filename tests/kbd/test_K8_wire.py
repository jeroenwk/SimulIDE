"""K8 (specs/keyboard-input.md): wiring pinFrom to pinTo with _.

Expected values follow the spec: _ connects the two remembered pins as the CLI wire
add does (H/V route, a dot inserted on a pin that already has a wire, one undo step);
the new wire is not selected and the pins and the selection stay; nothing is wired
(a note in Messages) when a pin is missing, both are the same pin, or they are already
wired; a running simulation is powered off first; a value being typed is reverted by _
before the wire is made. Nothing is simulated (the wire case powers the sim off).
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def placed(sim):
    """The uid of the one selected part after placing it with a key."""
    return sim.cli("kbd", "state")["selection"][0]


def ends(sim):
    """The wires as sorted (from, to) pairs."""
    return sorted( tuple(sorted( (w["from"], w["to"]) )) for w in sim.cli("wire", "list") )


def pane(sim):
    return str(sim.cli("messages"))


def test(sim):
    # r 1 k Enter, r 2 k Enter, _: R1.right wired to R2.left; the pins and the
    # selection stay, the wire is not selected
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    r1 = placed(sim)
    sim.cli("ui", "type", "1", "k", "enter")
    sim.cli("ui", "type", "r")
    r2 = placed(sim)
    sim.cli("ui", "type", "2", "k", "enter")
    s = sim.cli("ui", "type", "_")
    sim.check("_: wired R1.rPin to R2.lPin", int(ends(sim) == [(r1+".rPin", r2+".lPin")]), 1, absolute=0,
              note=str(ends(sim)))
    sim.check("_: pinFrom stays", int(s["pinFrom"] == r1+".rPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("_: pinTo stays", int(s["pinTo"] == r2+".lPin"), 1, absolute=0, note=str(s["pinTo"]))
    sim.check("_: nothing gets selected", len(s["selection"]), 0, absolute=0)

    # _ again: already wired, a note says so
    sim.cli("ui", "type", "_")
    sim.check("already wired: still one wire", len(ends(sim)), 1, absolute=0)
    sim.check("already wired: note in Messages", int("already wired" in pane(sim)), 1, absolute=0)

    # A pin that already has a wire: a junction dot is inserted on that wire
    sim.cli("ui", "click", "300,100")
    sim.cli("ui", "type", "r")
    r3 = placed(sim)
    sim.cli("select", r1)
    sim.cli("ui", "type", ">")                         # One step, so Cmd+> lands on rPin:
    sim.cli("ui", "type", "cmd+>")                     # the pin that has the wire, as pinFrom
    sim.check("pinFrom is R1.rPin", int(sim.cli("kbd", "state")["pinFrom"] == r1+".rPin"), 1, absolute=0)
    sim.cli("select", r3)                              # pinTo = R3's nearest pin
    s = sim.cli("kbd", "state")
    sim.check("junction setup: pinFrom R1.rPin", int(s["pinFrom"] == r1+".rPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.cli("ui", "type", "_")
    wires = ends(sim)
    sim.check("a junction was inserted", int(any(e[0].split(".")[0].startswith("Node") or
                                                  e[1].split(".")[0].startswith("Node") for e in wires)), 1,
              absolute=0, note=str(wires))

    # Cmd+Z removes the wire (one undo step): back to the single first wire
    sim.cli("ui", "type", "cmd+z")
    sim.check("cmd+z removes the wired junction", int(ends(sim) == [(r1+".rPin", r2+".lPin")]), 1, absolute=0,
              note=str(ends(sim)))

    # A BJT: > to the base, _ wires to it. (Placed after R1, the BJT is rotated
    # with its first pin, the collector, facing R1's pin — D17.)
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    r1 = placed(sim)
    sim.cli("ui", "type", "t")
    bjt = placed(sim)
    for _i in range(4):                                # Step until the base is selected
        s = sim.cli("ui", "type", ">")
        if s.get("selectedPin") == bjt+".base": break
    sim.check("the base is the selected pin", int(s["selectedPin"] == bjt+".base"), 1, absolute=0,
              note=str(s.get("selectedPin")))
    sim.cli("ui", "type", "_")
    sim.check("_ wires to the base", int(tuple(sorted((r1+".rPin", bjt+".base"))) in ends(sim)), 1, absolute=0,
              note=str(ends(sim)))

    # One opamp: > then Cmd+> , _ wires two of its own pins
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "u")
    op = placed(sim)
    sim.cli("ui", "type", ">", "cmd+>")
    sim.cli("ui", "type", "_")
    wires = ends(sim)
    sim.check("opamp: wired two of its own pins", int(len(wires) == 1 and
              all(p.split(".")[0] == op for pair in wires for p in pair)), 1, absolute=0, note=str(wires))

    # A value being typed is reverted by _, then the wire is made
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    r1 = placed(sim)
    sim.cli("ui", "type", "r")
    r2 = placed(sim)
    sim.cli("ui", "type", "5", "_")
    sim.check("_ mid-edit keeps the old value", int(sim.cli("comp", "get", r1, "Resistance")["properties"][0]["value"] == "100 Ω"), 1, absolute=0)
    sim.check("_ mid-edit wires anyway", int(ends(sim) == [(r1+".rPin", r2+".lPin")]), 1, absolute=0,
              note=str(ends(sim)))

    # Simulation running: powered off first, then wired
    sim.cli("select", "--none")
    sim.cli("select", r1)                              # pinFrom R1 -> pinTo R1's own other pin?
    sim.cli("circuit", "new", "--discard")             # Fresh: place two parts again
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    r1 = placed(sim)
    sim.cli("ui", "type", "r")
    r2 = placed(sim)
    sim.cli("ui", "type", "enter")                     # Unselect; the pins stay
    sim.cli("sim", "start")
    sim.cli("ui", "type", "_")
    sim.check("sim stopped to wire", int(sim.cli("sim", "status")["state"] == "stopped"), 1, absolute=0)
    sim.check("wired while the sim was running", int(ends(sim) == [(r1+".rPin", r2+".lPin")]), 1, absolute=0,
              note=str(ends(sim)))
    sim.check("note: stopped to wire", int("stopped to wire" in pane(sim)), 1, absolute=0)

    # No pins at all: a note, no wire
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "focus", "canvas")
    sim.cli("ui", "type", "_")
    sim.check("no pins: no wire", len(ends(sim)), 0, absolute=0)
    sim.check("no pins: note in Messages", int("nothing to wire" in pane(sim)), 1, absolute=0)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: _ wires", int(any(r["keys"] == "_" and "pinFrom" in r["action"] for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
