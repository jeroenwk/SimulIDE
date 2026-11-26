"""Photodiode model "Solar cell", and Isc / Voc properties that set Sensitivity and IS.

Scherz Fig. 5.21 needed nine Photodiodes with Sensitivity 100 uA/lx and IS 500 pA to make
0.5 V cells: it works but is not obvious. Now the Photodiode has `Isc` (short-circuit
current at 1 klx) and `Voc` (open-circuit voltage at 1 klx), which set Sensitivity = Isc /
1000 lx and IS = Isc / (e^(Voc/(N Vt)) - 1), Vt = 25.865 mV, and a model "Solar cell": a
small silicon cell, 100 mA and 0.60 V in full sun (100 klx) [INFERRED, typical 60 x 40 mm
cell]: Isc 1 mA and Voc 0.481 V at 1 klx. Expected by hand:
- Solar cell, 1 klx, open (1 G load): 0.481 V (the shunt 1 M takes 0.05 %); shorted by 1 ohm:
  1.000 mA (1 mV).
- Solar cell, 100 klx: open 0.600 V, into 1 ohm 100 mA (0.1 V: the diode still off).
- Custom with Isc 2 mA, Voc 0.5 V: open at 1 klx 0.500 V; 1 ohm: 2.0 mV.
- Scherz 5.21's cell (Sensitivity 100 uA/lx, IS 500 pA) reads Isc 100 mA, Voc 0.494 V.
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def cell(sim, props, lux, load):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0", *props)
    c("comp", "set", "D", "Max_Lux=100000")
    c("comp", "set", "D", "Lux=%g" % lux)
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,80", "Resistance=%g" % load)
    c("comp", "add", "Ground", "--label", "G", "--at", "80,160")
    c("wire", "add", "D.anode", "R.rPin")
    c("wire", "add", "D.cathode", "R.lPin")
    c("wire", "add", "D.cathode", "G.Gnd")
    c("sim", "run", "--for", "20ms")
    v = c("probe", "D.anode")[0]["voltage"]
    c("sim", "stop")
    return v


def test(sim):
    c = sim.cli
    try:
        v = cell(sim, ["Model=Solar cell"], 1000, 1e9)
    except Exception as e:
        sim.check("solar cell model (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    within(sim, "Solar cell 1 klx open (V)", v, 0.475, 0.487)
    within(sim, "Solar cell 1 klx into 1 ohm (mA)", cell(sim, ["Model=Solar cell"], 1000, 1) * 1e3, 0.99, 1.01)
    within(sim, "Solar cell 100 klx open (V)", cell(sim, ["Model=Solar cell"], 100000, 1e9), 0.594, 0.606)
    within(sim, "Solar cell 100 klx into 1 ohm (mA)", cell(sim, ["Model=Solar cell"], 100000, 1) * 1e3, 99, 101)
    within(sim, "Isc 2 mA, Voc 0.5 V: open at 1 klx (V)", cell(sim, ["Isc=2 mA", "Voc=0.5"], 1000, 1e9), 0.494, 0.506)
    within(sim, "Isc 2 mA, Voc 0.5 V: into 1 ohm (mA)", cell(sim, ["Isc=2 mA", "Voc=0.5"], 1000, 1) * 1e3, 1.98, 2.02)

    c("circuit", "new", "--discard")
    c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0", "Sensitivity=100000", "IS=5e-10")
    got = c("comp", "get", "D")
    txt = str(got)
    import re
    isc = re.search(r"'Isc'[^0-9]*([0-9.eE+-]+)", txt)
    voc = re.search(r"'Voc'[^0-9]*([0-9.eE+-]+)", txt)
    sim.check("Isc / Voc readable (%s)" % txt[:200], int(bool(isc and voc)), 1, absolute=0)
    if isc and voc:
        within(sim, "Scherz 5.21 cell: Isc (mA)", float(isc.group(1)), 99.9, 100.1)
        within(sim, "Scherz 5.21 cell: Voc (V)", float(voc.group(1)), 0.490, 0.498)


run_test(test)
