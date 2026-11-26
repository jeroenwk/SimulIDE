"""Diode model 1N5819 (1 A 40 V Schottky rectifier), Scherz Fig. 11.22's catch diode.

The library's only Schottky was the 1N5711 small-signal part (Rs 2.8 ohm): as an LM2575's
catch diode at 1 A it dropped about 4 V, so the switch node rang to -4 V instead of -0.4 V
(books/scherz/fig11_22_lm2575_buck.sim1, 2026-11-26).

Model "1N5819 Schottky": Is, N and Rs fitted through the datasheet's maximum instantaneous
forward voltages (onsemi 1N5817-D, 1N5819 column, pulse test): 0.34 V at 0.1 A, 0.60 V at
1 A, 0.90 V at 3 A. So the model is the worst-case part. Its Is (0.86 mA) is also the
datasheet's maximum reverse current at the rated 40 V (1.0 mA at 25 C): leakage at 40 V
must stay at or below 1 mA. Expected values are the datasheet's, within 0.02 V.
"""

from harness import run_test

MODEL = "1N5819 Schottky"


def vf(sim, amps):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Current Source", "--label", "I1", "--at", "-100,0", "Value_Amp=%g" % amps, "MaxValue=5",
      "Running=true")
    c("comp", "add", "Diode", "--label", "D1", "--at", "0,0", "Model=" + MODEL)
    c("comp", "add", "Ground", "--label", "G", "--at", "80,80")
    c("wire", "add", "I1.outPin", "D1.lPin")
    c("wire", "add", "D1.rPin", "G.Gnd")
    c("sim", "run", "--for", "100us")
    v = c("probe", "D1.lPin")[0]["voltage"]
    model = {p["name"]: p["value"] for p in c("comp", "get", "D1")["properties"]}.get("Model")
    c("sim", "stop")
    return v, model


def test(sim):
    v, model = vf(sim, 0.1)
    if model != MODEL:
        sim.check("Diode has Model=%s (got %s)" % (MODEL, model), 0, 1, absolute=0)
        return
    sim.check("VF at 0.1 A (V), datasheet max 0.34", v, 0.34, absolute=0.02)
    sim.check("VF at 1 A (V), datasheet max 0.60", vf(sim, 1.0)[0], 0.60, absolute=0.02)
    sim.check("VF at 3 A (V), datasheet max 0.90", vf(sim, 3.0)[0], 0.90, absolute=0.02)

    c = sim.cli                                     # reverse leakage at the rated 40 V
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-100,0", "Voltage=40", "Out=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "-40,0", "Resistance=1")
    c("comp", "add", "Diode", "--label", "D1", "--at", "40,0", "Model=" + MODEL)
    c("comp", "add", "Ground", "--label", "G", "--at", "120,80")
    c("wire", "add", "V.outnod", "R.lPin")
    c("wire", "add", "R.rPin", "D1.rPin")              # cathode to +40 V: reverse biased
    c("wire", "add", "D1.lPin", "G.Gnd")
    c("sim", "run", "--for", "100us")
    i = abs(c("read", "R")["current"])
    c("sim", "stop")
    sim.check("reverse current at 40 V (mA) <= 1.0 (datasheet max)", int(i <= 1.0e-3), 1, absolute=0,
              note="%.3f mA" % (i * 1e3))


run_test(test)
