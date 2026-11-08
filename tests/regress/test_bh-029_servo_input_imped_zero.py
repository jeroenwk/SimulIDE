"""BH-029: Input Impedance 0 on a Servo (any IoComponent) makes the input pin voltages NaN.

IoComponent::setInputImp (src/gui/circuitwidget/components/iocomponent.cpp:188) hands the value
to IoPin::setInputImp (iopin.cpp:252) which stores m_gndAdmit = 1/imp: an infinite admittance.
Right result: a zero (or negative) input impedance is clamped to a tiny positive one and the
pins read finite, non-negative voltages (a negative impedance gave negative node voltages): three inputs each driven from 5 V / 2 V / 0.7 V through 1k.
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def pins(sim, imp):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Servo", "--label", "D", "--at", "0,0")
    sim.cli("comp", "set", "D", "Input_Imped=%s" % imp)
    refs = [p["ref"] for p in sim.cli("comp", "get", "D")["pins"]]
    for i, (ref, v) in enumerate(zip(refs, (5, 2, 0.7))):
        sim.cli("comp", "add", "Fixed Voltage", "--label", "V%d" % i, "--at", "-300,%d" % (i * 100), "Voltage=%s" % v, "Out=true")
        sim.cli("comp", "add", "Resistor", "--label", "R%d" % i, "--at", "-150,%d" % (i * 100), "Resistance=1k")
        sim.cli("wire", "add", "V%d.outnod" % i, "R%d.lPin" % i)
        sim.cli("wire", "add", "R%d.rPin" % i, ref)
    sim.cli("sim", "run", "--for", "2ms", timeout=40)
    return sim.read("D")["pins"]


def test(sim):
    for imp in ("1M", "0", "-1"):
        p = pins(sim, imp)
        ok = all(v is not None and not math.isnan(v) and -1e-6 <= v < 10 for v in p.values())
        sim.check("Servo Input_Imped=%s: pin voltages finite (%s)" % (imp, p), 1 if ok else 0, 1, tol=0)


run_test(test)
