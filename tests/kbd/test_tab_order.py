"""Tab order of the parts (specs/parts-pane.md §12).

Expected values follow the spec:
- without indices the walk is the reading order (rows top to bottom, left to right);
- Cmd+Shift+T: the selected parts follow each other in the order they were selected,
  as one block where the first of them was; Opt+Cmd+T to the front, Opt+Cmd+Shift+T to
  the back; a place typed (tab order set N, the Parts pane's Tab cell) shifts the others;
- each is one undo step; Tab walks that order; the Parts pane's Tab column shows it;
- indices are saved, copies are pasted without one (after the ordered parts), new parts
  come after them too, locked parts stay out, reset goes back to the reading order.
Nothing is simulated.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def ok(sim, name, cond, note=""):
    sim.check(name, int(bool(cond)), 1, absolute=0, note=str(note)[:300])


def walk(sim):
    return [p["label"] for p in sim.cli("tab", "order")["walk"]]


def test(sim):
    sim.cli("settings", "set", "flashSelect", "false")
    sim.cli("circuit", "new", "--discard")
    for i in range(1, 5):
        sim.cli("comp", "add", "Resistor", "--label", "R%d" % i, "--at", "%d,100" % (i * 100))
    ok(sim, "default: reading order", walk(sim) == ["R1", "R2", "R3", "R4"], walk(sim))

    # Cmd+Shift+T: as selected (R3 then R1), the block where R1 was
    sim.cli("ui", "click", "300,100")
    sim.cli("ui", "click", "100,100", "--cmd")
    r = sim.cli("tab", "order")
    ok(sim, "selection order tracked", r["selectionOrder"] == ["R3", "R1"], r["selectionOrder"])
    sim.cli("ui", "type", "cmd+shift+t")
    ok(sim, "cmd+shift+t: R3, R1 first", walk(sim) == ["R3", "R1", "R2", "R4"], walk(sim))
    msg = sim.cli("messages", "--grep", "Tab order")
    ok(sim, "message", msg, msg)
    sim.cli("undo")
    ok(sim, "one undo step", walk(sim) == ["R1", "R2", "R3", "R4"], walk(sim))
    sim.cli("redo")

    # Tab walks it, the pane shows it
    sim.cli("select", "--none")
    sim.cli("ui", "click", "100,300")
    seen = []
    for _ in range(4):
        s = sim.cli("ui", "type", "tab")
        seen.append(s["selection"][0])
    labels = {r["uid"]: r["label"] for r in sim.cli("parts", "list", "--all")}
    seen = [labels[u] for u in seen]
    start = seen.index("R3")
    ok(sim, "Tab walks the order", (seen[start:] + seen[:start]) == ["R3", "R1", "R2", "R4"], seen)
    tabs = {r["label"]: r["tab"] for r in sim.cli("parts", "list", "--all")}
    ok(sim, "pane Tab column", tabs == {"R3": 1, "R1": 2, "R2": 3, "R4": 4}, tabs)

    # Front / back
    sim.cli("select", "R4")
    sim.cli("ui", "type", "opt+cmd+t")
    ok(sim, "opt+cmd+t: R4 to front", walk(sim) == ["R4", "R3", "R1", "R2"], walk(sim))
    sim.cli("select", "R4", "R3")
    sim.cli("ui", "type", "opt+cmd+shift+t")
    ok(sim, "opt+cmd+shift+t: R4, R3 to back, order kept", walk(sim) == ["R1", "R2", "R4", "R3"], walk(sim))

    # A place typed: the others shift
    sim.cli("tab", "order", "set", "1", "R3")
    ok(sim, "set 1: R3 first, the others shift", walk(sim) == ["R3", "R1", "R2", "R4"], walk(sim))
    sim.cli("tab", "order", "set", "3", "R3")
    ok(sim, "set 3", walk(sim) == ["R1", "R2", "R3", "R4"], walk(sim))
    sim.cli("tab", "order", "set", "2", "R4,R1")
    ok(sim, "set 2 for two parts: a block", walk(sim) == ["R2", "R1", "R4", "R3"], walk(sim))

    # New parts and copies after the ordered ones; locked parts out
    sim.cli("comp", "add", "Resistor", "--label", "R0", "--at", "0,100")
    ok(sim, "new part at the end", walk(sim)[-1] == "R0", walk(sim))
    new = sim.cli("comp", "copy", "R2")
    ok(sim, "copy at the end", len(walk(sim)) == 6 and walk(sim)[:5] == ["R2", "R1", "R4", "R3", "R0"], (walk(sim), new))
    sim.cli("lock", "R1")
    ok(sim, "locked part out of the walk", "R1" not in walk(sim), walk(sim))
    sim.cli("unlock", "R1")
    ok(sim, "unlocked: back in its place", walk(sim)[:4] == ["R2", "R1", "R4", "R3"], walk(sim))

    # Saved
    d = tempfile.mkdtemp(prefix="tab-")
    path = os.path.join(d, "tab.sim1")
    before = walk(sim)
    sim.cli("circuit", "save", path)
    sim.cli("circuit", "open", path, "--discard")
    ok(sim, "kept after reopen", walk(sim) == before, (walk(sim), before))

    # Rubber band: selected in reading order; reset
    r = sim.cli("tab", "order", "reset")
    ok(sim, "reset: reading order", walk(sim)[:5] == ["R0", "R1", "R2", "R3", "R4"] and not r["custom"], walk(sim))
    sim.cli("select", "--none")
    sim.cli("ui", "drag", "250,60", "450,140")
    r = sim.cli("tab", "order")
    ok(sim, "rubber band: reading order", r["selectionOrder"] == ["R3", "R4"], r["selectionOrder"])
    sim.cli("select", "R3")
    sim.cli("ui", "type", "cmd+shift+t")
    ok(sim, "cmd+shift+t with one part: nothing changes", not sim.cli("tab", "order")["custom"])


if __name__ == "__main__":
    run_test(test)
