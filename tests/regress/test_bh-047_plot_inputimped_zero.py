"""BH-047: Impedance 0 on an Oscope / Logic Analyzer makes the input pin voltages NaN.

PlotBase::setInputImped (src/gui/dataplotwidget/plotbase.cpp:157) stores m_inputAdmit = 1/i
without a check: i = 0 gives an infinite admittance (the same class as BH-029/030/031).
Right result: a zero (or negative) impedance is clamped to a tiny positive one and the pins
read finite voltages: each input driven from 5 V / 2 V / 0.7 V through 1k.
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def pins(sim, part, imp):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", part, "--label", "D", "--at", "0,0")
    sim.cli("comp", "set", "D", "InputImped=%s" % imp)
    refs = [p["ref"] for p in sim.cli("comp", "get", "D")["pins"]]
    for i, (ref, v) in enumerate(zip(refs, (5, 2, 0.7))):
        sim.cli("comp", "add", "Fixed Voltage", "--label", "V%d" % i, "--at", "-300,%d" % (i * 100), "Voltage=%s" % v, "Out=true")
        sim.cli("comp", "add", "Resistor", "--label", "R%d" % i, "--at", "-150,%d" % (i * 100), "Resistance=1k")
        sim.cli("wire", "add", "V%d.outnod" % i, "R%d.lPin" % i)
        sim.cli("wire", "add", "R%d.rPin" % i, ref)
    sim.cli("sim", "run", "--for", "2ms", timeout=40)
    return sim.read("D")["pins"]


def test(sim):
    for part in ("Oscope", "LAnalizer"):
        for imp in ("1M", "0", "-1"):
            p = pins(sim, part, imp)
            v3 = [p[k] for k in sorted(p)[:3]]
            ok = all(v is not None and not math.isnan(v) and -1e-6 <= v < 10 for v in v3)
            sim.check("%s InputImped=%s: pin voltages finite (%s)" % (part, imp, v3), 1 if ok else 0, 1, tol=0)


run_test(test)
