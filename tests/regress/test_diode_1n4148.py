"""Diode model 1N4148 / 1N914 (create-circuit #115).

The commonest small-signal diode in book circuits was missing from the Model list: the
default silicon model (EmCoef 2, 171 nA) reads 0.46 V at 1.4 mA, so two of them clamped
a reference at -0.92 V instead of a book's -1.2 V.

Model "1N4148 1N914 signal" (the common SPICE model: Is 2.52 nA, N 1.752, Rs 0.568 ohm;
VRRM 100 V), forward voltage with a current source, expected from the diode equation
V = N Vt ln(I/Is) + I Rs, and within the datasheet's VF <= 1.0 V at 10 mA:
1 mA 0.585 V, 10 mA 0.694 V, 100 mA 0.85 V.
"""

from harness import run_test


def vf(sim, amps):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Current Source", "--label", "I1", "--at", "-100,0", "Value_Amp=%g" % amps, "MaxValue=1",
      "Running=true")
    c("comp", "add", "Diode", "--label", "D1", "--at", "0,0", "Model=1N4148 1N914 signal")
    c("comp", "add", "Ground", "--label", "G", "--at", "80,80")
    c("wire", "add", "I1.outPin", "D1.lPin")
    c("wire", "add", "D1.rPin", "G.Gnd")
    c("sim", "run", "--for", "100us")
    v = c("probe", "D1.lPin")[0]["voltage"]
    c("sim", "stop")
    return v


def test(sim):
    try:
        v = vf(sim, 1e-3)
    except Exception as e:
        sim.check("Diode accepts Model=1N4148 1N914 signal (%s)" % str(e)[-120:], 0, 1, absolute=0)
        return
    sim.check("VF at 1 mA (V)", v, 0.585, absolute=0.01)
    v = vf(sim, 10e-3)
    sim.check("VF at 10 mA (V)", v, 0.694, absolute=0.01)
    sim.check("VF at 10 mA within datasheet max 1.0 V", int(v <= 1.0), 1, absolute=0)
    sim.check("VF at 100 mA (V)", vf(sim, 0.1), 0.850, absolute=0.015)


run_test(test)
