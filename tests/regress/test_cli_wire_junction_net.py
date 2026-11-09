"""CLI `wire add` to a pin whose own wire is too short for a junction (bug hunt IMP-022).

The automatic junction goes on a straight segment of at least 16 px of the pin's wire. A pin
joined to a junction by an 8 px stub has none, and wire add used to fail ("no wire segment long
enough") although the rest of the net had plenty of room; book builds hit it in nearly every
circuit. Now it falls back to the nearest splittable segment of the same net.

Circuit: B1 (10 V) -> R1 -> long wire -> R2 -> ground. R3's left pin hangs on that wire by an
8 px stub (@80,0 junction). wire add R4.rPin R3.lPin must then succeed and join the same net:
with R4 (1 kOhm) to ground, the net is R1 (1 kOhm) from 10 V against R2 || R4 (500 Ohm) to
ground, so it sits at 10 x 500 / 1500 = 3.333 V and R4 carries 3.333 mA.
"""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "-80,40", "Voltage=10")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "0,0", "Resistance=1000")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "200,0", "Resistance=1000")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "96,8", "Resistance=1000")
    sim.cli("comp", "add", "Resistor", "--label", "R4", "--at", "96,120", "Resistance=1000")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,200")
    sim.cli("wire", "add", "B1.lPin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "R2.lPin")          # Straight along y=0, x 16..184
    stub = sim.cli("wire", "add", "R3.lPin", "@80,0")     # R3.lPin is at 80,8: an 8 px stub
    sim.log("stub: %s" % stub)
    sim.cli("wire", "add", "R2.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    sim.cli("wire", "add", "R4.lPin", "G.Gnd")

    w = sim.cli("wire", "add", "R4.rPin", "R3.lPin")     # Used to fail: R3's wire is 8 px long
    sim.log("R4 to R3.lPin's net: %s" % w)

    sim.run("10ms")
    r4 = sim.read("R4")
    sim.check("R4 voltage (net at 10 x 500/1500)", abs(r4["voltage"]), 10 * 500 / 1500, tol=0.01)
    sim.check("R4 current", abs(r4["current"]), 10 / 1500 / 2, tol=0.01)


if __name__ == "__main__":
    run_test(test)
