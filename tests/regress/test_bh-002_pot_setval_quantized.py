"""BH-002: a Potentiometer's "Value_Ohm" set from code is quantized to 1/1000 of the resistance.

1 kΩ pot, Value_Ohm=2.5 must give R(A-wiper)=2.5 Ω, but getVal()/updateStep() read the
integer dial (0..1000), so it becomes 2 Ω (and 0.4 Ω becomes 1 mΩ). Wiper to ground,
A to a 10 V source: I = 10/R.
"""

from harness import run_test


def current(sim, ohm):
    sim.cli("comp", "set", "P1", "Value_Ohm=%s" % ohm)
    sim.run("5ms")
    return abs(sim.cli("comp", "get", "P1")["pins"][0]["current"])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Potentiometer", "--label", "P1", "--at", "0,0", "Resistance=1000")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=10")
    sim.cli("wire", "add", "B1.rPin", "P1.%s" % "PinA")
    sim.cli("wire", "add", "P1.PinM", "G.Gnd")
    sim.cli("wire", "add", "B1.lPin", "G.Gnd")
    for ohm in (2.5, 123.4):
        sim.check("pot A-wiper current at %s ohm" % ohm, current(sim, ohm), 10.0 / ohm, tol=0.01)


if __name__ == "__main__":
    run_test(test)
