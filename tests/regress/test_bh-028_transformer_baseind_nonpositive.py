"""BH-028: a Transformer with Base Inductance 0 or negative gives NaN or 1e192 V.

Transformer::setBaseInd (src/gui/circuitwidget/components/passive/reactive/transformer.cpp:192)
stores any value and the coils are built with it, so the inductance is zero/negative.
Right result: a non-positive inductance is clamped to a tiny positive one and the circuit
solves with finite voltages: 5 V through 1k into the primary, secondary loaded by 1k, all
node voltages stay within 0..5 V (plus a margin).
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def volts(sim, base):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-300,0", "Voltage=5", "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-300,100")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "-150,0", "Resistance=1k")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "150,0", "Resistance=1k")
    sim.cli("comp", "add", "Transformer", "--label", "T", "--at", "0,0")
    sim.cli("comp", "set", "T", "BaseInd=%s" % base)
    sim.cli("wire", "add", "V.outnod", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "T.P00Pin")
    sim.cli("wire", "add", "T.P01Pin", "G.Gnd")
    sim.cli("wire", "add", "T.S00Pin", "R2.lPin")
    sim.cli("wire", "add", "R2.rPin", "G.Gnd")
    sim.cli("wire", "add", "T.S01Pin", "G.Gnd")
    sim.cli("sim", "run", "--for", "2ms", timeout=40)
    return sim.read("T")["pins"]


def test(sim):
    for base in (1, 0, -1):
        p = volts(sim, base)
        ok = all(v is not None and not math.isnan(v) and abs(v) < 10 for v in p.values())
        sim.check("Transformer BaseInd=%s node voltages finite and < 10 V (%s)" % (base, p), 1 if ok else 0, 1, tol=0)


run_test(test)
