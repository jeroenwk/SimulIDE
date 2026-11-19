"""K9e (specs/keyboard-input.md): the component list from the keyboard.

Expected values follow the spec: : selects the next part in the component list and /
the previous one (categories skipped, wrapping), the canvas keeping the keyboard; k
places the part selected in the list as a part letter does — at the cross, or
continuing the chain from a selected part's pin; k with nothing selected in the list
gives a message. Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def lib(s):
    return s.get("libraryPart")


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")

    # k with nothing selected in the list: a message, nothing placed
    s = sim.cli("ui", "type", "k")
    sim.check("k, no list part: nothing placed", len(sim.cli("comp", "list")), 0, absolute=0)
    sim.check("k, no list part: message", int("no part selected in the component list" in str(sim.cli("messages"))),
              1, absolute=0)

    # : and / step through the list; the canvas keeps the keys
    s = sim.cli("ui", "type", ":")
    first = lib(s)
    sim.check(": selects the first part", int(first is not None and first["name"] == "Probe"), 1, absolute=0,
              note=str(first))
    sim.check(": the canvas keeps the focus", int(s["focused"]), 1, absolute=0)
    s = sim.cli("ui", "type", ":")
    sim.check(": the next part", int(lib(s)["name"] == "Voltimeter"), 1, absolute=0, note=str(lib(s)))
    s = sim.cli("ui", "type", "/")
    sim.check("/ the previous part", int(lib(s) == first), 1, absolute=0, note=str(lib(s)))
    s = sim.cli("ui", "type", "/")
    sim.check("/ from the first: wraps to the last", int(lib(s) != first and lib(s) is not None), 1, absolute=0,
              note=str(lib(s)))
    s = sim.cli("ui", "type", ":")
    sim.check(": from the last: wraps to the first", int(lib(s) == first), 1, absolute=0, note=str(lib(s)))

    # k places it at the cross, selected
    s = sim.cli("ui", "type", ":", ":")                  # Amperimeter
    sim.check("setup: the ammeter selected in the list", int(lib(s)["name"] == "Amperimeter"), 1, absolute=0,
              note=str(lib(s)))
    s = sim.cli("ui", "type", "k")
    comps = sim.cli("comp", "list")
    sim.check("k: one part placed", len(comps), 1, absolute=0)
    uid = s["selection"][0] if s["selection"] else ""
    sim.check("k: an ammeter, selected", int(uid.startswith("Amperimeter")), 1, absolute=0, note=uid)
    pos = sim.cli("comp", "get", uid)["pos"] if uid else [0, 0]
    sim.check("k: at the cross", int(abs(pos[0] - 100) <= 8 and abs(pos[1] - 100) <= 8), 1, absolute=0, note=str(pos))

    # With a part selected, k continues the chain as a part letter does (D17)
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")                            # R1, its rPin selected (pointing right)
    r1 = sim.cli("kbd", "state")["selection"][0]
    rpin = [p for p in sim.cli("comp", "get", r1)["pins"] if p["id"].endswith("rPin")][0]["pos"]
    s = sim.cli("ui", "type", "k")
    new = s["selection"][0]
    sim.check("k after R1: a new ammeter selected", int(new.startswith("Amperimeter")), 1, absolute=0, note=new)
    sim.check("k after R1: its pinTo faces R1's pin", int(s["pinFrom"] is not None and s["pinTo"] is not None), 1,
              absolute=0, note="%s %s" % (s["pinFrom"], s["pinTo"]))
    pins = sim.cli("comp", "get", new)["pins"]
    sim.check("k after R1: a pin 64 px beyond R1.rPin, on its row",
              int(any(p["pos"] == [rpin[0] + 64, rpin[1]] for p in pins)), 1, absolute=0,
              note=str([p["pos"] for p in pins]))

    # A subcircuit from the list
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "300,300")
    found = None
    for _ in range(400):
        s = sim.cli("ui", "type", ":")
        if lib(s)["type"] == "Subcircuit":
            found = lib(s)
            break
    sim.check("a subcircuit in the list", int(found is not None), 1, absolute=0)
    if found:
        s = sim.cli("ui", "type", "k")
        comps = sim.cli("comp", "list")
        sim.check("k: the subcircuit placed", int(len(comps) >= 1 and s["selection"] != []), 1, absolute=0,
                  note="%s %s" % (found, s["selection"]))

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: k", int(any(r["keys"] == "k" for r in sc)), 1, absolute=0)
    sim.check("registry: : /", int(any(r["keys"] == ": / /" for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
