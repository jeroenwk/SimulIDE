"""Junction capacitance of a diode, C(V) = Cj0/(1 - V/Vj)^M (docs/NOISE.md).

Uses the values measured on the led_driver board's D3A, an ES1J (led_driver SPEC
test 22, ring test against T1): Cj0 14.67 pF, Vj 0.363 V, M 0.319 [MEASURED fit].
30 V charges the reverse-biased diode through 1 MΩ from 0 V:
C(V_R)·dV_R/dt = (30 V - V_R)/1 MΩ. The expected V_R(t) comes from integrating that
equation here (RK4, 0.1 ns): 7.106 V at 2 µs, 17.92 V at 5 µs, 26.67 V at 10 µs.
reactStep 10 ns (≥ 360 steps per RC); tolerance 1 %.

- Cj0 = 0 (default): no capacitance, the cathode is at 30 V after 2 µs.
- Cj0, Vj and Mj survive save and reopen.
"""

import os
import tempfile
from harness import run_test

CJ0, VJ, M, VB, R = 14.67e-12, 0.363, 0.319, 30.0, 1e6


def cap(vr):
    return CJ0 / (1 + vr / VJ) ** M


def expected(ts):
    out, v, t, h = [], 0.0, 0.0, 1e-10
    for T in ts:
        while t < T - 1e-15:
            k = lambda vv: (VB - vv) / R / cap(vv)
            a = k(v); b = k(v + h / 2 * a); c = k(v + h / 2 * b); d = k(v + h * c)
            v += h / 6 * (a + 2 * b + 2 * c + d)
            t += h
        out.append(v)
    return out


def build(sim, *props):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=30")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,-40", "Resistance=1000000")
    c("comp", "add", "Diode", "--label", "D1", "--at", "80,40", "--rot", "270", "SatCurrent=1 nA", *props)
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "D1.rPin")
    c("wire", "add", "D1.lPin", "G.Gnd")
    c("settings", "set", "reactStep=10ns")


def test(sim):
    build(sim)
    sim.cli("sim", "run", "--for", "2us")
    sim.check("Cj0 = 0: no capacitance (V at 2 µs)", sim.cli("probe", "D1.rPin")[0]["voltage"], 30.0, tol=0.001)
    sim.cli("sim", "stop")

    build(sim, "Cj0=14.67 pF", "Vj=0.363", "Mj=0.319")
    ts = [2e-6, 5e-6, 10e-6, 20e-6]
    prev = 0.0
    for T, e in zip(ts, expected(ts)):
        sim.cli("sim", "run", "--for", "%gus" % ((T - prev) * 1e6))
        prev = T
        v = sim.cli("probe", "D1.rPin")[0]["voltage"]
        sim.check("ES1J C(V): V_R at %g µs" % (T * 1e6), v, e, tol=0.01)
    sim.cli("sim", "stop")

    p = os.path.join(tempfile.mkdtemp(prefix="junction-"), "d.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "D1")["properties"]}
    sim.check("reopened Cj0", 1 if props.get("Cj0") == "14.67 pF" else 0, 1, absolute=0)
    sim.check("reopened Vj", 1 if props.get("Vj") == "0.363 V" else 0, 1, absolute=0)
    sim.check("reopened Mj", 1 if props.get("Mj") == "0.319" else 0, 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
