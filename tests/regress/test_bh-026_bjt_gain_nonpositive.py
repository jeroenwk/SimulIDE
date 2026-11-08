"""BH-026: a BJT with Gain 0 (or -1) makes every node voltage NaN.

eBJT::setGain (src/simulator/elements/active/e-bjt.cpp:164) stores any value and
m_fgain = Gain/(Gain+1) is 0 (Gain 0) or infinite (Gain -1); eBJT::voltChanged divides by
m_fgain, the solver gets NaN and reports "Not converging".
Right result: a non-positive current gain is clamped to a small positive one, the solver
keeps working and the voltages are finite. Circuit: 10 V - 1k - collector, base through
100k from 5 V, emitter grounded; the collector voltage must lie between 0 V and 10 V.
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def collector(sim, gain):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "-200,0", "Voltage=10", "Out=true")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "VB", "--at", "-200,200", "Voltage=5", "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-200,100")
    sim.cli("comp", "add", "Resistor", "--label", "RC", "--at", "-100,0", "Resistance=1k")
    sim.cli("comp", "add", "Resistor", "--label", "RB", "--at", "-100,200", "Resistance=100k")
    sim.cli("comp", "add", "BJT", "--label", "Q", "--at", "0,0", "Gain=%s" % gain)
    sim.cli("wire", "add", "VC.outnod", "RC.lPin")
    sim.cli("wire", "add", "RC.rPin", "Q.collector")
    sim.cli("wire", "add", "Q.emiter", "G.Gnd")
    sim.cli("wire", "add", "VB.outnod", "RB.lPin")
    sim.cli("wire", "add", "RB.rPin", "Q.base")
    sim.run("5ms")
    return sim.read("Q")["pins"]["collector"]


def test(sim):
    # sanity: a normal gain gives the textbook value, Ic = 100*(5-0.634)/100k = 4.37 mA
    sim.check("BJT Gain=100 collector voltage", collector(sim, 100), 5.63, tol=0, absolute=0.1)
    for g in (0, -1, -5):
        v = collector(sim, g)
        ok = v is not None and not math.isnan(v) and -0.01 <= v <= 10.01
        sim.check("BJT Gain=%g collector voltage within 0..10 V (got %s)" % (g, v), 1 if ok else 0, 1, tol=0)


run_test(test)
