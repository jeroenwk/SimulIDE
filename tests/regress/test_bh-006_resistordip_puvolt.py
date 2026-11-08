"""BH-006: ResistorDip Pullup Voltage changed while running is not applied.

setPuVolt stores the value but does not mark the part changed, so updateStep never restamps.
Pullup dip resistor 1 kohm from Vpu to a 1 kohm load to ground: node = Vpu / 2.
Needs the pins of BH-005 (stacked on that branch).
"""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "ResistorDip", "--label", "D1", "--at", "0,0", "Resistance=1000", "Size=2", "PullUp=true", "PuVolt=5")
    pins = sim.cli("comp", "get", "D1")["pins"]
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,0", "Resistance=1000")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,100")
    sim.cli("wire", "add", pins[0]["ref"], "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "G.Gnd")
    sim.run("2ms")
    sim.check("PuVolt 5 V: node (V)", sim.read("R1")["pins"]["lPin"], 2.5, tol=0.01)
    sim.cli("comp", "set", "D1", "PuVolt=10")
    sim.run("2ms")
    sim.check("PuVolt 10 V: node (V)", sim.read("R1")["pins"]["lPin"], 5.0, tol=0.01)


if __name__ == "__main__":
    run_test(test)
