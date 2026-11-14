"""Parallel (parasitic) capacitance of a resistor, property Cp (docs/NOISE.md, Junction capacitance).

5 V step into a divider: R1 1 MΩ with Cp 10 pF on top, R2 1 MΩ and C2 100 pF below.
The capacitive divider sets the node at once to 5·10/110 = 0.4545 V, then it moves
to 2.5 V with τ = (R1 ∥ R2)(Cp + C2) = 55 µs:
V(t) = 2.5 − (2.5 − 0.4545)·exp(−t/τ). reactStep 10 ns.

- Cp gives the same node voltage, sample by sample, as a separate 10 pF capacitor
  (within 0.1 mV: the two drift apart by ≈ 7e-6 of the value over 250 µs, well inside
  backward Euler's own error h/2τ ≈ 1e-4 at this step; the separate capacitor has an
  inner node and events of its own).
- The curve matches V(t) within 1 %.
- Cp 0 (default): the node starts from 0 V (C2 alone), as before.
- Cp survives save and reopen.
"""

import math
import os
import tempfile
from harness import run_test

TAU = 0.5e6 * 110e-12
V0 = 5 * 10 / 110


def build(sim, cp=None, cap=False):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=5")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,-40", "Resistance=1000000",
      *(["Cp=%s" % cp] if cp else []))
    c("comp", "add", "Resistor", "--label", "R2", "--at", "80,40", "--rot", "90", "Resistance=1000000")
    c("comp", "add", "Capacitor", "--label", "C2", "--at", "140,40", "--rot", "90", "Capacitance=100 pF")
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "R2.lPin")
    c("wire", "add", "R2.rPin", "G.Gnd")
    c("wire", "add", "C2.lPin", "R1.rPin")
    c("wire", "add", "C2.rPin", "G.Gnd")
    if cap:
        c("comp", "add", "Capacitor", "--label", "C1", "--at", "0,-100", "Capacitance=10 pF")
        c("wire", "add", "C1.lPin", "R1.lPin")
        c("wire", "add", "C1.rPin", "R1.rPin")
    c("settings", "set", "reactStep=10ns")


def trace(sim, steps_us=(0.02, 10, 30, 60, 120, 250)):
    out, prev = [], 0.0
    for t in steps_us:
        sim.cli("sim", "run", "--for", "%gus" % (t - prev))
        prev = t
        out.append(sim.cli("probe", "R1.rPin")[0]["voltage"])
    sim.cli("sim", "stop")
    return out


def test(sim):
    ts = (0.02, 10, 30, 60, 120, 250)
    build(sim, cp="10 pF")
    a = trace(sim, ts)
    build(sim, cap=True)
    b = trace(sim, ts)
    worst = max(abs(x - y) for x, y in zip(a, b))
    sim.check("Cp = separate capacitor (worst difference, V)", worst, 0.0, absolute=1e-4)
    for t, v in zip(ts, a):
        e = 2.5 - (2.5 - V0) * math.exp(-t * 1e-6 / TAU)
        sim.check("V at %g µs" % t, v, e, tol=0.01)

    build(sim)
    z = trace(sim, ts[:1])
    sim.check("Cp = 0: node starts from 0 V (V at 20 ns)", z[0], 0.0, absolute=0.01)

    build(sim, cp="10 pF")
    p = os.path.join(tempfile.mkdtemp(prefix="resistor-cp-"), "r.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "R1")["properties"]}
    sim.check("reopened Cp", 1 if props.get("Cp") == "10 pF" else 0, 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
