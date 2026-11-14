"""Parallel (winding) capacitance of an inductor, property Cp (docs/NOISE.md, Junction capacitance).

The led_driver's T1 off the board (SPEC test 21): 374 µH with 17.9 pF of winding
capacitance [MEASURED]. A 5 V step through 100 kΩ rings the tank at
1/(2π√(LC)) = 1.946 MHz. reactStep 1 ns.

- Cp 17.9 pF gives the same node voltage, sample by sample, as Cp 0 with a 17.9 pF
  capacitor across the inductor.
- The ring has the expected period (zero crossings of V − its final value).
- Cp 0 (default): no ring, the node decays monotonically.
- Cp survives save and reopen.
"""

import math
import os
import tempfile
from harness import run_test

L, C = 374e-6, 17.9e-12
F0 = 1 / (2 * math.pi * math.sqrt(L * C))


def build(sim, cp=None, cap=False):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=5")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,-40", "Resistance=100000")
    props = ["Inductance=374 µH"] + (["Cp=%s" % cp] if cp else [])
    c("comp", "add", "Inductor", "--label", "L1", "--at", "80,40", "--rot", "90", *props)
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "L1.lPin")
    c("wire", "add", "L1.rPin", "G.Gnd")
    if cap:
        c("comp", "add", "Capacitor", "--label", "C1", "--at", "140,40", "--rot", "90", "Capacitance=17.9 pF")
        c("wire", "add", "C1.lPin", "R1.rPin")
        c("wire", "add", "C1.rPin", "G.Gnd")
    c("settings", "set", "reactStep=1ns")


def trace(sim, n=60, dt_ns=20):
    out = []
    for _ in range(n):
        sim.cli("sim", "run", "--for", "%dns" % dt_ns)
        out.append(sim.cli("probe", "R1.rPin")[0]["voltage"])
    sim.cli("sim", "stop")
    return out


def test(sim):
    build(sim, cp="17.9 pF")
    a = trace(sim)
    build(sim, cap=True)
    b = trace(sim)
    worst = max(abs(x - y) for x, y in zip(a, b))
    sim.check("Cp = separate capacitor (worst difference, V)", worst, 0.0, absolute=1e-6)

    # Zero crossings of the ring (final value 0 V across the inductor)
    zc = [i for i in range(1, len(a)) if (a[i - 1] > 0) != (a[i] > 0)]
    if len(zc) >= 3:
        half = (zc[-1] - zc[0]) * 20e-9 / (len(zc) - 1)
        sim.check("ring frequency (Hz)", 1 / (2 * half), F0, tol=0.05)
    else:
        sim.check("ring has zero crossings", len(zc), 3, absolute=0)

    build(sim)
    z = trace(sim)
    sim.check("Cp = 0: no ring (sign changes)", sum(1 for i in range(1, len(z)) if (z[i - 1] > 0) != (z[i] > 0)), 0, absolute=0)

    build(sim, cp="17.9 pF")
    p = os.path.join(tempfile.mkdtemp(prefix="inductor-cp-"), "l.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "L1")["properties"]}
    sim.check("reopened Cp", 1 if props.get("Cp") == "17.9 pF" else 0, 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
