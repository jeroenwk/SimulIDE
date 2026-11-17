"""`sim average` on a meter averages between its terminals (create-circuit #471).

An Amperimeter or Voltimeter has three pins (lPin, rPin and its output outnod), so
`sim average A1` stopped with "A1 has 3 pins: name a pin", and a pin gives a node voltage,
not the meter's current: a build fell back on the instantaneous `read`.

Right result: 10 V -> 1 kOhm -> ammeter A1 -> ground: its average current is 10 mA (sign:
into lPin) and the voltage across it ~0; a voltmeter VM across the resistor averages
10 V between its terminals. A resistor (two pins) is unchanged.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B", "--at", "-160,0", "Voltage=10")
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,-80", "Resistance=1k")
    c("comp", "add", "Amperimeter", "--label", "A1", "--at", "120,0")
    c("comp", "add", "Voltimeter", "--label", "VM", "--at", "0,-200")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,160")
    c("wire", "add", "B.lPin", "R.lPin")
    c("wire", "add", "R.rPin", "A1.lPin")
    c("wire", "add", "A1.rPin", "G.Gnd")
    c("wire", "add", "B.rPin", "G.Gnd")
    c("wire", "add", "VM.lPin", "R.lPin")
    c("wire", "add", "VM.rPin", "R.rPin")
    r = c("sim", "average", "--for", "2ms", "A1", "VM", "R")
    a, v, rr = r["channels"]
    sim.log("A1 %s | VM %s | R %s" % (a, v, rr))
    sim.check("ammeter mean current (A)", abs(a["current"]["mean"]), 0.010, tol=0.001)
    sim.check("ammeter burden < 1 mV", int(abs(a["voltage"]["mean"]) < 1e-3), 1, absolute=0)
    sim.check("voltmeter across R (V)", abs(v["voltage"]["mean"]), 10.0, tol=0.001)
    sim.check("resistor still averages (V)", abs(rr["voltage"]["mean"]), 10.0, tol=0.001)


if __name__ == "__main__":
    run_test(test)
