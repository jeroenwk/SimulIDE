"""Winding capacitance of a relay coil, property Cp (create-circuit #150).

A relay coil switched off with no diode: its inner inductor alone gives a spike one
reactive step wide (the current has nowhere to go). A real coil has winding
capacitance, which turns that into a ring. The plain Inductor has it as `Cp`; the
relay's coil (an Inductor inside the part) gets the same property.

Same tank as test_inductor_cp: 374 µH, 17.9 pF, 1 Ω, a 5 V step through 100 kΩ
(50 µA: far below IOn, so the contacts never move). reactStep 1 ns.

- The relay with Cp 17.9 pF gives the same coil voltage, sample by sample, as an
  Inductor with the same L, R and Cp.
- Cp 0 (default): no ring.
- Cp survives save and reopen.
"""

import os
import tempfile
from harness import run_test


def build(sim, relay=True, cp=None):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=5")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,-40", "Resistance=100000")
    extra = ["Cp=%s" % cp] if cp else []
    if relay:
        c("comp", "add", "RelaySPST", "--label", "K1", "--at", "80,40", "Inductance=0.374 mH", "Rcoil=1", *extra)
        a, b = "K1.inductor-lPin", "K1.inductor-rPin"
    else:
        c("comp", "add", "Inductor", "--label", "K1", "--at", "80,40", "--rot", "90",
          "Inductance=374 µH", "Resistance=1000000 µΩ", *extra)
        a, b = "K1.lPin", "K1.rPin"
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", a)
    c("wire", "add", b, "G.Gnd")
    c("settings", "set", "reactStep=1ns")


def trace(sim, n=60, dt_ns=20):
    out = []
    for _ in range(n):
        sim.cli("sim", "run", "--for", "%dns" % dt_ns)
        out.append(sim.cli("probe", "R1.rPin")[0]["voltage"])
    sim.cli("sim", "stop")
    return out


def crossings(v):
    return sum(1 for i in range(1, len(v)) if (v[i - 1] > 0) != (v[i] > 0))


def test(sim):
    build(sim, relay=False, cp="17.9 pF")
    ref = trace(sim)
    sim.check("reference Inductor rings (sign changes)", min(crossings(ref), 3), 3, absolute=0)

    try:
        build(sim, cp="17.9 pF")
        got = trace(sim)
    except Exception as e:                              # Cp not a relay property yet
        sim.check("relay accepts Cp (%s)" % str(e)[:60], 0, 1, absolute=0)
        return
    worst = max(abs(x - y) for x, y in zip(got, ref))
    sim.check("relay Cp = Inductor Cp (worst difference, V)", worst, 0.0, absolute=1e-6)

    build(sim)
    sim.check("Cp = 0: no ring (sign changes)", crossings(trace(sim)), 0, absolute=0)

    build(sim, cp="17.9 pF")
    p = os.path.join(tempfile.mkdtemp(prefix="relay-cp-"), "k.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "K1")["properties"]}
    sim.check("reopened Cp", 1 if props.get("Cp") == "17.9 pF" else 0, 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
