"""BH-034: BJT terminal currents are stale (up to the 10 mV convergence window).

eBJT::voltChanged (src/simulator/elements/active/e-bjt.cpp) returns early when the junction
voltages moved less than 10 mV since the last iteration, before setTrueCurrent() runs, so
the currents `comp get` shows come from an iteration up to 10 mV (about 47 % in current)
away from the converged voltages.
Circuit: 12 V - 11.3k - Q1 (collector tied to base), Q2 base on the same node, 5k load.
Both transistors have Vbe = 0.596 V and Vbc = 0 (Q1), so Ic1 must equal Ic2 within a few %
and Ic1 + Ib1 + Ib2 must equal the current in the 11.3k resistor.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V1", "--at", "0,-160", "Voltage=12", "Out=true")
    sim.cli("comp", "add", "Resistor", "--label", "RREF", "--at", "-40,-60", "--rot", "90", "Resistance=11.3k")
    sim.cli("comp", "add", "Resistor", "--label", "RL", "--at", "120,-60", "--rot", "90", "Resistance=5k")
    sim.cli("comp", "add", "BJT", "--label", "Q1", "--at", "-16,0", "Gain=100")
    sim.cli("comp", "add", "BJT", "--label", "Q2", "--at", "80,0", "Gain=100")
    sim.cli("comp", "add", "Ground", "--label", "G1", "--at", "-8,80")
    sim.cli("comp", "add", "Ground", "--label", "G2", "--at", "88,80")
    sim.cli("wire", "add", "V1.outnod", "RREF.lPin", "--via", "-40,-160")
    sim.cli("wire", "add", "V1.outnod", "RL.lPin", "--via", "120,-160")
    sim.cli("wire", "add", "RREF.rPin", "Q1.collector")
    sim.cli("wire", "add", "RL.rPin", "Q2.collector")
    sim.cli("wire", "add", "Q1.base", "Q2.base", "--via", "-40,0,-40,-30,56,-30,56,0")
    sim.cli("wire", "add", "Q1.base", "Q1.collector", "--via", "-40,0,-40,-24,-8,-24")
    sim.cli("wire", "add", "Q1.emiter", "G1.Gnd")
    sim.cli("wire", "add", "Q2.emiter", "G2.Gnd")
    sim.run("5ms")
    q1 = sim.read("Q1")
    q2 = sim.read("Q2")
    iref = sim.read("RREF")["current"]
    print("Q1 ic=%g ib=%g  Q2 ic=%g ib=%g  Iref=%g" % (q1["i_collector"], q1["i_base"], q2["i_collector"], q2["i_base"], iref))
    # same Vbe and Vbc = 0 for Q1: both collector currents agree (Q2 differs only by Early-like 1/rgain)
    sim.check("Q1 Ic equals Q2 Ic", q1["i_collector"], q2["i_collector"], tol=0.05)
    # KCL on the mirror node: Iref = Ic1 + Ib1 + Ib2
    sim.check("Iref = Ic1 + Ib1 + Ib2", q1["i_collector"] + q1["i_base"] + q2["i_base"], abs(iref), tol=0.03)


run_test(test)
