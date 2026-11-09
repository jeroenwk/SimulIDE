"""BH-032 (guard for the fix): an op amp with positive feedback still snaps between its rails.

The fix stamps the op amp's linear region in the matrix. With positive feedback (an inverting
Schmitt trigger: output -> R1 -> V+ -> R2 -> ground) the linear solution is the unstable middle
point, so the model must detect the unstable loop and go to the other rail. Right: the output is
only ever at +15 V or -15 V (RMS 15 V), switching every half period of a 10 V, 1 kHz sine. Wrong: it
sits in between, following the input.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    for args in [
        ("WaveGen", "--label", "W", "--at", "-200,40", "Wave_Type=Sine", "Freq=1000", "Mid_Volt=0", "Semi_Ampli=10", "Always_On=true"),
        ("OpAmp", "--label", "U1", "--at", "-40,32", "Gain=100000", "Volt_Pos=15", "Volt_Neg=-15"),
        ("Resistor", "--label", "R1", "--at", "-40,-24", "Resistance=10k"),
        ("Resistor", "--label", "R2", "--at", "-120,-24", "Resistance=10k"),
        ("Ground", "--label", "G1", "--at", "-160,0"),
    ]:
        c("comp", "add", *args)
    for a in [("W.outnod", "U1.inputInv"), ("U1.output", "R1.rPin", "--via", "-8,-24"),
              ("R1.lPin", "U1.inputNinv"), ("R1.lPin", "R2.rPin"), ("R2.lPin", "G1.Gnd")]:
        c("wire", "add", *a)
    av = c("sim", "average", "--for", "5ms", "U1.output", timeout=60)
    v = av["channels"][0]["voltage"]
    sim.log("U1.output %s, %s steps" % (v, av.get("steps")))
    sim.check("output RMS: only at the rails (V)", v["rms"], 15.0, absolute=0.01)
    sim.check("output max (V)", v["max"], 15.0, absolute=0.01)
    sim.check("output min (V)", v["min"], -15.0, absolute=0.01)
    sim.check("output mean, symmetric (V)", v["mean"], 0.0, absolute=0.05)
    n = str(c("log")).count("Not converging")
    sim.check("'Not converging' faults", n, 0, absolute=0.5)


if __name__ == "__main__":
    run_test(test)
