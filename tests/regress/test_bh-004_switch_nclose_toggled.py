"""BH-004: changing Norm_Close on a toggled Switch ignores the button state.

MechContact::setNClose calls setSwitch(m_nClose), which assumes the button is not toggled.
A toggled switch is closed when Normally Closed = false and open when it is true, so:
  toggled, NC false -> true : must become open (stays closed with the bug)
  toggled, NC true -> false : must become closed (becomes open with the bug)
Check: 10 V over Switch + 1 kohm: 10 mA closed, 0 open (Ohm's law).
"""

from harness import run_test


def ma(sim):
    return abs(sim.read("R1")["current"]) * 1000


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=10")
    sim.cli("comp", "add", "Switch", "--label", "S1", "--at", "80,-40")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "160,-40", "Resistance=1000")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("wire", "add", "B1.lPin", "S1.pinP0")
    sim.cli("wire", "add", "S1.switch0pinN", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    sim.cli("comp", "set", "S1", "Toggled=true")
    sim.run("2ms")
    sim.check("toggled, NC false: closed (mA)", ma(sim), 10.0, tol=0.01)
    sim.cli("comp", "set", "S1", "Norm_Close=true")
    sim.run("2ms")
    sim.check("toggled, NC true: open (mA+1)", ma(sim) + 1.0, 1.0, tol=0.01)
    sim.cli("comp", "set", "S1", "Norm_Close=false")
    sim.run("2ms")
    sim.check("toggled, NC back to false: closed (mA)", ma(sim), 10.0, tol=0.01)


if __name__ == "__main__":
    run_test(test)
