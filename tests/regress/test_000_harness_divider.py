"""Harness self-check (not a bug): 10 V across 1 kΩ + 1 kΩ gives 5 mA and 5 V per resistor."""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=10")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "160,-40", "Resistance=1000")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("wire", "add", "B1.lPin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "R2.lPin")
    sim.cli("wire", "add", "R2.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    sim.run("10ms")
    r1 = sim.read("R1")
    sim.check("R1 current", abs(r1["current"]), 0.005, tol=0.01)
    sim.check("R1 voltage", abs(r1["voltage"]), 5.0, tol=0.01)


if __name__ == "__main__":
    run_test(test)
