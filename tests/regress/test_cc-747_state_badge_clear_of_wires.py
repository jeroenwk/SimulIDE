"""A part's state badge is drawn where no wire runs (create-circuit #747).

The badge (Show State) sat centred above the part, over the wire into a transistor's top pin
(the collector wire): in 444 places in the book circuits a capture taken while running hid
part of a wire. It now takes the first place clear of wires: above, above left / right of the
centre line, beside, below. `comp get` gives it as `badge` while it is shown.
Expected: an NPN with its collector wired straight up to RC: the badge (ACTIVE) is shown and no
wire segment crosses it; with nothing above the part it stays centred above it.
"""
from harness import run_test


def hits(rect, wires):
    x, y, w, h = rect
    for wr in wires:
        p = wr["points"]
        for (x0, y0), (x1, y1) in zip(zip(p[0::2], p[1::2]), zip(p[2::2], p[3::2])):
            if max(x0, x1) >= x and min(x0, x1) <= x + w and max(y0, y1) >= y and min(y0, y1) <= y + h:
                return True
    return False


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "BJT", "--label", "Q1", "--at", "0,0")
    c("comp", "add", "Rail", "--label", "V", "--at", "-120,-120", "Voltage=5")
    c("comp", "add", "Resistor", "--label", "RB", "--at", "-64,0", "Resistance=100k")
    c("comp", "add", "Resistor", "--label", "RC", "--at", "8,-80", "--rot", "90", "Resistance=1k")
    c("comp", "add", "Ground", "--label", "G", "--at", "8,64")
    for a, b in (("V.outnod", "RB.lPin"), ("RB.rPin", "Q1.base"), ("RC.lPin", "Q1.collector"),
                 ("V.outnod", "RC.rPin"), ("Q1.emiter", "G.Gnd")):
        c("wire", "add", a, b)
    c("sim", "start")
    c("sim", "run", "--for", "5ms")
    q = c("comp", "get", "Q1")
    badge = q.get("badge")
    sim.check("badge shown while running", int(bool(badge)), 1, absolute=0)
    sim.check("no wire under the badge %s" % badge, int(bool(badge) and not hits(badge, c("wire", "list"))), 1, absolute=0)
    c("sim", "stop")

    c("circuit", "new", "--discard")                 # nothing above: centred above the body
    c("comp", "add", "BJT", "--label", "Q1", "--at", "0,0")
    c("comp", "add", "Rail", "--label", "V", "--at", "-120,40", "Voltage=5")
    c("comp", "add", "Resistor", "--label", "RB", "--at", "-64,0", "Resistance=100k")
    c("comp", "add", "Resistor", "--label", "RC", "--at", "64,-16", "Resistance=1k")
    c("comp", "add", "Ground", "--label", "G", "--at", "8,64")
    for a, b in (("V.outnod", "RB.lPin"), ("RB.rPin", "Q1.base"), ("RC.lPin", "Q1.collector"),
                 ("V.outnod", "RC.rPin"), ("Q1.emiter", "G.Gnd")):
        c("wire", "add", a, b)
    c("sim", "start")
    c("sim", "run", "--for", "5ms")
    q = c("comp", "get", "Q1")
    b, body = q.get("badge") or [0, 0, 0, 0], q["body"]
    sim.check("free above: centred over the body (x)", b[0] + b[2] / 2, body[0] + body[2] / 2, absolute=1)
    sim.check("free above: above the body", int(b[1] + b[3] < body[1]), 1, absolute=0)
    c("sim", "stop")


run_test(test)
