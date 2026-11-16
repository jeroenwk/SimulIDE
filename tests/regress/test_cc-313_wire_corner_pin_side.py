"""wire add turns its one corner the way that leaves both pins outwards (create-circuit #313).

Between two pins that differ in x and y, wire add made an L whose first leg followed the
start pin's axis, whichever way the pin faces: from a tunnel facing right towards a pin
on its left, the wire ran back through the tunnel's own body (E-THROUGH in the checker).
Of the two L's, the one that neither leaves the start pin backwards nor enters the end
pin from behind is taken; the start pin's axis still decides when both are fine.
"""

from harness import run_test


def corners(sim, a, b):
    pts = sim.cli("wire", "add", a, b)["points"]
    return [tuple(pts[i:i + 2]) for i in range(2, len(pts) - 2, 2)]


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Tunnel", "--label", "T1", "--at", "-40,-40", "Name=A")    # pin -40,-40 facing right
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-96,40", "--rot", "90")  # lPin -96,24 facing up
    k = corners(sim, "T1.pin", "R1.lPin")
    sim.check("tunnel facing away: corner below the tunnel pin %s" % k, int(k == [(-40, 24)]), 1, absolute=0)

    c("comp", "add", "Tunnel", "--label", "T2", "--at", "40,-40", "Name=B")     # pin 40,-40 facing right
    c("comp", "add", "Resistor", "--label", "R2", "--at", "120,40", "--rot", "90")  # lPin 120,24 facing up
    k = corners(sim, "T2.pin", "R2.lPin")
    sim.check("tunnel facing its target: along the pin first %s" % k, int(k == [(120, -40)]), 1, absolute=0)


run_test(test)
