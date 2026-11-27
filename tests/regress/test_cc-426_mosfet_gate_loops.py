"""Loops through a MOSFET's gate converge with no gate capacitance (create-circuit #426, #529).

The MOSFET stamped its channel as RDSon and a current source, with no gate term in the matrix: a
loop through the gate was solved by fixed-point iteration, which diverges at loop gain > 1. With
the default Cgs = Cgd = 0:
- #426 (Scherz fig 4.99): an op amp regulating a depletion FET's source current latched at a
  rail, the FET off (12 nA for 1 mA), and stayed there; a 1 uF gate capacitor was the workaround;
- #529 (AoE fig 12.45B): a pMOS high-side switch with a pnp robbing its gate (current limit) did
  not converge at t=0 (100001 iterations: Q1, Q2, Q3 still iterating) and settled non-physical
  (the gate at 683 V, 0 A or 18 A for a ~4.57 A limit); 1 nF / 100 pF was the workaround.

Expected, Cgs = Cgd = 0:
- current source: VIN 1 V, RS 1k, load 1k from 12 V, op amp Gain 1000 (+12/-5 V), depletion FET
  (RDSon 1k, Threshold 3 V): 1 mA in the load (0.999 with the 1/1001 gain error), op amp LINEAR;
- high-side limit: 24 V, RS 0.13 ohm, 2N3906 across it robbing the gate, R2 36k gate-source,
  2N3904 sinking 0.27 mA (3.3 V, 10k), pMOS RDSon 0.2 ohm Threshold 3 V; the same current as with
  the gate capacitances at each load: 4.564 A at RL 2.7 ohm (12.3 V across it), 4.571 A at 1 ohm,
  4.574 A at 0.01 ohm; and no "Not converging" in Messages.
"""
from harness import run_test


def source(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "opAmp", "--label", "OA", "--at", "-16,0", "Gain=1000", "Volt_Pos=12", "Volt_Neg=-5")
    c("comp", "add", "Mosfet", "--label", "Q", "--at", "48,0", "Depletion=true", "RDSon=1k", "Threshold=3")
    c("comp", "add", "Rail", "--label", "VDD", "--at", "48,-144", "Voltage=12")
    c("comp", "add", "Resistor", "--label", "LOAD", "--at", "48,-80", "--rotate", "90", "Resistance=1k")
    c("comp", "add", "Rail", "--label", "VIN", "--at", "-100,-64", "Voltage=1")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "48,64", "--rotate", "90", "Resistance=1k")
    c("comp", "add", "Ground", "--label", "G", "--at", "48,140")
    c("wire", "add", "VIN.outnod", "OA.inputNinv")
    c("wire", "add", "Q.Gate", "OA.output")
    c("wire", "add", "LOAD.lPin", "VDD.outnod")
    c("wire", "add", "LOAD.rPin", "Q.Dren")
    c("wire", "add", "RS.rPin", "G.Gnd")
    c("wire", "add", "OA.inputInv", "Q.Sour")
    c("wire", "add", "RS.lPin", "Q.Sour")
    c("sim", "run", "--for", "20ms")
    i = c("probe", "LOAD.lPin")[0]["current"]
    st = c("read", "OA").get("state")
    c("sim", "stop")
    return i, st


def highside(sim, rl):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "V24", "--at", "-64,-176", "Voltage=24")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "0,-144", "Resistance=0.13")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "-64,-100", "--rotate", "90", "Resistance=36k")
    c("comp", "add", "BJT", "--label", "Q3", "--at", "-16,-96", "Model=2N3906")
    c("comp", "add", "BJT", "--label", "Q1", "--at", "-64,32", "Model=2N3904")
    c("comp", "add", "Mosfet", "--label", "Q2", "--at", "48,-96", "P_Channel=true", "RDSon=0.2", "Threshold=3")
    c("comp", "add", "Fixed Voltage", "--label", "IN", "--at", "-140,32", "Voltage=3.3", "Out=true")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-64,96", "--rotate", "90", "Resistance=10k")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "112,16", "--rotate", "90", "Resistance=%g" % rl)
    c("comp", "add", "Ground", "--label", "G", "--at", "0,180")
    c("wire", "add", "V24.outnod", "RS.lPin")
    c("wire", "add", "RS.rPin", "Q2.Sour")
    c("wire", "add", "RS.rPin", "Q3.base")
    c("wire", "add", "Q3.emiter", "RS.lPin")
    c("wire", "add", "R2.lPin", "V24.outnod")
    c("wire", "add", "R2.rPin", "Q2.Gate")
    c("wire", "add", "Q3.collector", "Q2.Gate")
    c("wire", "add", "Q1.collector", "Q2.Gate")
    c("wire", "add", "IN.outnod", "Q1.base")
    c("wire", "add", "Q1.emiter", "R1.lPin")
    c("wire", "add", "R1.rPin", "G.Gnd")
    c("wire", "add", "Q2.Dren", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    n0 = len(c("messages")["lines"])
    c("sim", "run", "--for", "20ms")
    i = c("probe", "RL.lPin")[0]["current"]
    bad = sum("Not converging" in l for l in c("messages")["lines"][n0:])
    c("sim", "stop")
    return i, bad


def test(sim):
    i, st = source(sim)
    sim.check("current source, Cgs 0: load current (A)", i, 0.999e-3, absolute=5e-6)
    sim.check("current source: op amp LINEAR", 1 if st == "LINEAR" else 0, 1, absolute=0, note=str(st))
    for rl, want in ((2.7, 4.564), (1, 4.571), (0.01, 4.574)):
        i, bad = highside(sim, rl)
        sim.check("high-side limit, Cgs/Cgd 0, RL %g: current (A)" % rl, i, want, absolute=0.01)
        sim.check("high-side limit, RL %g: no 'Not converging'" % rl, bad, 0, absolute=0)


run_test(test)
