"""BH-057: BJT Newton loop accepts a solution up to 10 mV from its linearization point.

eBJT::voltChanged (src/simulator/elements/active/e-bjt.cpp) stops iterating once Vbe and Vbc
moved less than 10 mV since the last stamp. The node voltages then come from the linearized
model around the previous point, while the currents reported by `comp get` (BH-034) are the
exact exp() currents at the solved voltages: a 10 mV gap is up to ~8 % in current, so the
readouts break KCL against the resistors around the transistor.
Circuit (AoE fig. 2.74, Darlington): VB 5 V - RB 1M - Q1 base; Q1 emitter -> Q2 base;
both collectors -> RC 100 ohm -> VCC 10 V; Q2 emitter to ground; a scope (10 Mohm inputs)
on the collectors and on Q2's base, as in the circuit where it was found (without the scope
the loop happens to stop closer to the solution and the error is 0.07 %).
KCL: Ib1 = I(RB), Ic1 + Ic2 = I(RC).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Rail", "--label", "VCC", "--at", "320,176", "--rot", "90", "Voltage=10")
    sim.cli("comp", "add", "Rail", "--label", "VB", "--at", "224,256", "--rot", "90", "Voltage=5")
    sim.cli("comp", "add", "Resistor", "--label", "RC", "--at", "320,240", "--rot", "90", "Resistance=100")
    sim.cli("comp", "add", "Resistor", "--label", "RB", "--at", "192,304", "Resistance=1M")
    sim.cli("comp", "add", "BJT", "--label", "Q1", "--at", "256,336", "Gain=100")
    sim.cli("comp", "add", "BJT", "--label", "Q2", "--at", "320,336", "Gain=100")
    sim.cli("comp", "add", "Ground", "--label", "G1", "--at", "328,384")
    sim.cli("wire", "add", "VCC.outnod", "RC.lPin")
    sim.cli("wire", "add", "VB.outnod", "RB.rPin", "--via", "224,304")
    sim.cli("wire", "add", "RB.lPin", "Q1.base", "--via", "168,304,168,336")
    sim.cli("wire", "add", "Q1.emiter", "Q2.base", "--via", "264,368,296,368,296,336")
    sim.cli("wire", "add", "Q1.collector", "RC.rPin", "--via", "264,288,328,288")
    sim.cli("wire", "add", "Q2.collector", "RC.rPin", "--via", "328,288")
    sim.cli("wire", "add", "Q2.emiter", "G1.Gnd")
    sim.cli("comp", "add", "Oscope", "--label", "SC", "--at", "528,272")
    sim.cli("wire", "add", "SC.Pin0", "RC.rPin")
    sim.cli("wire", "add", "SC.Pin1", "Q2.base")
    sim.run("5ms")
    q1 = sim.read("Q1")
    q2 = sim.read("Q2")
    irb = abs(sim.read("RB")["current"])
    irc = abs(sim.read("RC")["current"])
    print("Q1 ib=%g ic=%g  Q2 ib=%g ic=%g  I(RB)=%g I(RC)=%g"
          % (q1["i_base"], q1["i_collector"], q2["i_base"], q2["i_collector"], irb, irc))
    sim.check("Ib1 = I(RB)", abs(q1["i_base"]), irb, tol=0.01)
    sim.check("Ic1 + Ic2 = I(RC)", abs(q1["i_collector"] + q2["i_collector"]), irc, tol=0.01)


run_test(test)
