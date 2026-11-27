"""VoltReg minimum load: the regulator's operating current leaves by its output (create-circuit #508).

A real LM317 needs a minimum load (3.5 mA typ, 10 mA max): its own operating current comes out
of the output pin and the pass transistor can only source, so below that load the output
rises. That is why the book picks R1 = 124 ohm (10 mA). The model regulated down to 0.1 mA,
both ways (it could sink), so the divider's real job could not be shown.

Min_Load (A, 0 = the old model) is that current. Expected by hand, Voltage=1.25 between
output and ADJ, R1 out-ADJ, R2 ADJ-ground, Vin 30 V, Min_Load 5 mA:
- R1 124, R2 372: divider 10.1 mA > 5 mA, regulates 1.25*(1+372/124) = 5.00 V;
- R1 1.2k, R2 3.6k: divider 1.04 mA < 5 mA, the pass is off and the 5 mA flow into the
  4.8k divider: 24.0 V (state not REGULATING);
- the same with a 1k load on the output: 5 mA + 1 mA > 5 mA, regulates 5.00 V again;
- Min_Load 0, R1 1.2k, R2 3.6k: 5.00 V (unchanged model).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def reg(sim, r1, r2, props=(), load=None):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "VIN", "--at", "-200,0", "Voltage=30", "Out=true")
    c("comp", "add", "VoltReg", "--label", "U", "--at", "0,0", "Voltage=1.25", *props)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "60,60", "Resistance=%g" % r1)
    c("comp", "add", "Resistor", "--label", "R2", "--at", "0,120", "--rotate", "90", "Resistance=%g" % r2)
    c("comp", "add", "Ground", "--label", "G", "--at", "0,200")
    c("wire", "add", "VIN.outnod", "U.input")
    c("wire", "add", "U.output", "R1.rPin")
    c("wire", "add", "R1.lPin", "U.ref")
    c("wire", "add", "U.ref", "R2.lPin")
    c("wire", "add", "R2.rPin", "G.Gnd")
    if load:
        c("comp", "add", "Resistor", "--label", "RL", "--at", "120,100", "--rotate", "90", "Resistance=%g" % load)
        c("comp", "add", "Ground", "--label", "G2", "--at", "120,200")
        c("wire", "add", "U.output", "RL.lPin")
        c("wire", "add", "RL.rPin", "G2.Gnd")
    sim.run("1ms")
    v = c("probe", "U.output")[0]["voltage"]
    st = sim.read("U").get("state")
    c("sim", "stop")
    return v, st


def test(sim):
    v, st = reg(sim, 124, 372, ("Min_Load=5m",))
    sim.check("R1 124: regulates (%s)" % st, v, 5.0, tol=0, absolute=2e-3)
    v, st = reg(sim, 1200, 3600, ("Min_Load=5m",))
    sim.check("R1 1.2k: under minimum load, output rises (%s)" % st, v, 24.0, tol=0, absolute=0.05)
    sim.check("R1 1.2k: not REGULATING (%s)" % st, int(not str(st).startswith("REGULATING")), 1, absolute=0)
    v, st = reg(sim, 1200, 3600, ("Min_Load=5m",), load=1000)
    sim.check("R1 1.2k + 1k load: regulates (%s)" % st, v, 5.0, tol=0, absolute=2e-3)
    v, st = reg(sim, 1200, 3600)
    sim.check("Min_Load 0: old model (%s)" % st, v, 5.0, tol=0, absolute=2e-3)


run_test(test)
