"""VoltReg: negative regulators, a Dropout setting and the ADJ-pin current (create-circuit #325, #306, #305).

The VoltReg compared its input with 0.7 V in absolute terms, so with a negative setpoint
(a 7915: Voltage=-15, input on the -20 V raw rail) it read OFF and its output followed the
input (#325, #335). Its dropout was fixed at 0.7 V (an LM317 needs about 2 V, #306), and it
had no ADJ-pin current (I_adj 50 uA, #305).

Right result (ref pin grounded unless said, 1k load):
- Voltage=5, Vin=10: 5 V; Vin=5.3: 4.6 V (default Dropout 0.7 V, unchanged);
- Dropout=2, Vin=6: 4 V;
- Voltage=-15, Vin=-20: -15 V, state REGULATING; Vin=-15.3: -14.6 V (mirrored dropout);
- LM317 divider (Voltage=1.25 between output and ADJ, R1 240 out-ADJ, R2 2.4k ADJ-ground),
  I_adj=50u: 1.25 (1 + 2400/240) + 50e-6 * 2400 = 13.87 V; with I_adj 0: 13.75 V.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def reg(sim, vin, props=(), lm317=False):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "VIN", "--at", "-200,0", "Voltage=%g" % vin, "Out=true")
    c("comp", "add", "VoltReg", "--label", "U", "--at", "0,0", *props)
    c("comp", "add", "Resistor", "--label", "RL", "--at", "100,100", "--rotate", "90", "Resistance=1k")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,200")
    c("comp", "add", "Ground", "--label", "G2", "--at", "100,200")
    c("wire", "add", "VIN.outnod", "U.input")
    c("wire", "add", "U.output", "RL.lPin")
    c("wire", "add", "RL.rPin", "G2.Gnd")
    if lm317:
        c("comp", "add", "Resistor", "--label", "R1", "--at", "60,60", "Resistance=240")
        c("comp", "add", "Resistor", "--label", "R2", "--at", "0,120", "--rotate", "90", "Resistance=2.4k")
        c("wire", "add", "U.output", "R1.rPin")
        c("wire", "add", "R1.lPin", "U.ref")
        c("wire", "add", "U.ref", "R2.lPin")
        c("wire", "add", "R2.rPin", "G.Gnd")
    else:
        c("wire", "add", "U.ref", "G.Gnd")
    sim.run("1ms")
    r = sim.read("U")
    return sim.read("RL")["pins"]["lPin"], r.get("state")


def test(sim):
    v, _ = reg(sim, 10, ("Voltage=5",))
    sim.check("7805 at 10 V in", v, 5.0, tol=0, absolute=1e-3)
    v, _ = reg(sim, 5.3, ("Voltage=5",))
    sim.check("7805 at 5.3 V in: default 0.7 V dropout", v, 4.6, tol=0, absolute=1e-3)
    v, _ = reg(sim, 6, ("Voltage=5", "Dropout=2"))
    sim.check("Dropout=2 at 6 V in", v, 4.0, tol=0, absolute=1e-3)
    v, st = reg(sim, -20, ("Voltage=-15",))
    sim.check("7915 at -20 V in (state %s)" % st, v, -15.0, tol=0, absolute=1e-3)
    sim.check("7915 state is REGULATING (%s)" % st, int(str(st).startswith("REGULATING")), 1, absolute=0)
    v, _ = reg(sim, -15.3, ("Voltage=-15",))
    sim.check("7915 at -15.3 V in: mirrored dropout", v, -14.6, tol=0, absolute=1e-3)
    v, _ = reg(sim, 20, ("Voltage=1.25",), lm317=True)
    sim.check("LM317 divider, I_adj 0", v, 13.75, tol=0, absolute=2e-3)
    v, _ = reg(sim, 20, ("Voltage=1.25", "I_adj=50u"), lm317=True)
    sim.check("LM317 divider, I_adj 50 uA", v, 13.87, tol=0, absolute=2e-3)


run_test(test)
