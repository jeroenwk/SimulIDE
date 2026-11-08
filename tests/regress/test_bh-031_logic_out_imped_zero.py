"""BH-031: Output Impedance 0 (or negative) on a logic part gives NaN / wrong pin voltages.

IoPin::setOutputImp (src/gui/circuitwidget/iopin.cpp:262) sets m_vddAdmit = 1/imp, an infinite
(or negative) admittance; IoComponent::setOutImp passes the value on unchecked.
Right result: a non-positive output impedance is clamped to a tiny positive one: an And Gate
with both inputs at 5 V, output loaded by 1k to ground, outputs ~5 V (not NaN, not negative).
With the default 40 ohm: 5 * 1000/1040 = 4.81 V.
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def out_volt(sim, imp):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "And Gate", "--label", "U", "--at", "0,0")
    sim.cli("comp", "set", "U", "Out_Imped=%s" % imp)
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-300,0", "Voltage=5", "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-300,100")
    sim.cli("comp", "add", "Resistor", "--label", "RL", "--at", "300,0", "Resistance=1k")
    pins = sim.cli("comp", "get", "U")["pins"]
    ins = [p["ref"] for p in pins if p["id"].startswith("in")]
    out = [p["ref"] for p in pins if p["id"].startswith("out")][0]
    for i, ref in enumerate(ins):
        sim.cli("comp", "add", "Resistor", "--label", "RI%d" % i, "--at", "-150,%d" % (i * 60), "Resistance=1")
        sim.cli("wire", "add", "V.outnod", "RI%d.lPin" % i)
        sim.cli("wire", "add", "RI%d.rPin" % i, ref)
    sim.cli("wire", "add", out, "RL.lPin")
    sim.cli("wire", "add", "RL.rPin", "G.Gnd")
    sim.cli("sim", "run", "--for", "2ms", timeout=40)
    return sim.read("U")["pins"][[p["id"] for p in pins if p["id"].startswith("out")][0]]


def test(sim):
    sim.check("And Gate Out_Imped=40: output voltage", out_volt(sim, 40), 5 * 1000 / 1040.0, tol=0.03)
    for imp in ("0", "-1"):
        v = out_volt(sim, imp)
        ok = v is not None and not math.isnan(v) and 4.5 < v < 5.0001
        sim.check("And Gate Out_Imped=%s: output voltage ~5 V (got %s)" % (imp, v), 1 if ok else 0, 1, tol=0)


run_test(test)
