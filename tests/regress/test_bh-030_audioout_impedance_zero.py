"""BH-030: Audio Out with Impedance 0 (or negative) gives NaN / negative pin voltages.

AudioOut::setImpedance (src/gui/circuitwidget/components/outputs/audio_out.cpp:200) stores any
value, and the element admittance is 1/Impedance. Right result: a non-positive impedance is
clamped to a tiny positive one: 5 V through 1k into the part with the other pin on ground
gives a finite voltage between 0 and 5 V on the driven pin (8 ohm: 5*8/1008 = 0.04 V).
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def volt(sim, imp):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-300,0", "Voltage=5", "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-300,100")
    sim.cli("comp", "add", "Resistor", "--label", "R", "--at", "-150,0", "Resistance=1k")
    sim.cli("comp", "add", "AudioOut", "--label", "A", "--at", "0,0")
    sim.cli("comp", "set", "A", "Impedance=%s" % imp)
    sim.cli("wire", "add", "V.outnod", "R.lPin")
    sim.cli("wire", "add", "R.rPin", "A.lPin")
    sim.cli("wire", "add", "A.rPin", "G.Gnd")
    sim.cli("sim", "run", "--for", "2ms", timeout=40)
    return sim.read("A")["pins"]["lPin"]


def test(sim):
    sim.check("AudioOut Impedance=8: driven pin voltage", volt(sim, 8), 5 * 8 / 1008.0, tol=0.05)
    for imp in ("0", "-1"):
        v = volt(sim, imp)
        ok = v is not None and not math.isnan(v) and -1e-6 <= v < 5.01
        sim.check("AudioOut Impedance=%s: pin voltage finite and in 0..5 V (got %s)" % (imp, v), 1 if ok else 0, 1, tol=0)


run_test(test)
