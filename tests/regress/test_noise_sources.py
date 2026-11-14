"""Transient noise sources (docs/NOISE_SPEC.md §3.2, §5.3): which parts make noise
and how much, from their value and operating point.

Circuit: 10 V → R1 4.7 kΩ → D1 → ground; 10 V → RC 1 kΩ → Q1 collector, base via
RB 100 kΩ from 10 V, emitter to ground; a 10 kΩ potentiometer P1 across the
supply; the battery and an ammeter-free ground make no noise.

- R1, RC, RB: thermal S = 4kT/R (T = 300.15 K), the potentiometer two halves.
- D1: shot S = 2q·I_D, I_D from `probe`.
- Q1: shot 2q·I_C (C–E) and 2q·I_B (B–E), from `read`.
- The battery makes none.
- noiseOff by label (R1), by type (type:Resistor removes RC, RB and R1 but keeps
  the potentiometer), and a typo (type:Resistr) removes nothing and is reported.
"""

from harness import run_test

KT4 = 4 * 1.380649e-23 * 300.15
Q2 = 2 * 1.602176634e-19


def build(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=10")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,-40", "Resistance=4700")
    c("comp", "add", "Diode", "--label", "D1", "--at", "80,-40")
    c("comp", "add", "Resistor", "--label", "RC", "--at", "160,-40", "Resistance=1000")
    c("comp", "add", "Resistor", "--label", "RB", "--at", "240,-40", "Resistance=100000")
    c("comp", "add", "BJT", "--label", "Q1", "--at", "200,40")
    c("comp", "add", "Potentiometer", "--label", "P1", "--at", "320,-40", "Resistance=10000")
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "D1.lPin")
    c("wire", "add", "D1.rPin", "G.Gnd")
    c("wire", "add", "R1.lPin", "RC.lPin")
    c("wire", "add", "RC.rPin", "Q1.collector")
    c("wire", "add", "R1.lPin", "RB.lPin")
    c("wire", "add", "RB.rPin", "Q1.base")
    c("wire", "add", "Q1.emiter", "G.Gnd")
    c("wire", "add", "R1.lPin", "P1.PinA")
    c("wire", "add", "P1.PinB", "G.Gnd")


def sources(sim, off="none"):
    sim.cli("sim", "stop")
    sim.cli("settings", "set", "noise=on", "noiseSeed=1", "noiseOff=" + off)
    sim.cli("sim", "run", "--for", "50us")
    r = sim.cli("noise", "sources")
    return r["sources"], r["noise"]


def test(sim):
    build(sim)
    src, _ = sources(sim)
    by = {}
    for s in src:
        by.setdefault((s["part"], s["kind"]), []).append(s)

    for lab, r in (("R1", 4700), ("RC", 1000), ("RB", 100000)):
        s = by.get((lab, "thermal"), [None])[0]
        sim.check("%s thermal S = 4kT/R" % lab, s and s["psd"], KT4 / r, tol=1e-6)
    pot = by.get(("P1", "thermal"), [])
    sim.check("P1: two thermal halves", len(pot), 2, absolute=0)
    sim.check("P1 halves in series: sum of R from S", sum(KT4 / s["psd"] for s in pot), 10000, tol=1e-3)
    sim.check("battery makes no noise", sum(1 for s in src if s["part"] == "B1"), 0, absolute=0)

    i_d = abs(sim.cli("probe", "D1.lPin")[0]["current"])
    s = by.get(("D1", "shot"), [None])[0]
    sim.check("D1 shot S = 2q·I_D", s and s["psd"], Q2 * i_d, tol=0.01)

    q = sim.cli("read", "Q1")
    shots = sorted(s["psd"] for s in by.get(("Q1", "shot"), []))
    sim.check("Q1: two shot sources", len(shots), 2, absolute=0)
    if len(shots) == 2:
        sim.check("Q1 base shot S = 2q·I_B", shots[0], Q2 * abs(q["i_base"]), tol=0.01)
        sim.check("Q1 collector shot S = 2q·I_C", shots[1], Q2 * abs(q["i_collector"]), tol=0.01)

    src, n = sources(sim, "R1")
    sim.check("noiseOff=R1 removes R1", sum(1 for s in src if s["part"] == "R1"), 0, absolute=0)
    sim.check("noiseOff=R1 keeps RC", sum(1 for s in src if s["part"] == "RC"), 1, absolute=0)

    src, n = sources(sim, "type:Resistor")
    sim.check("type:Resistor removes all resistors", sum(1 for s in src if s["type"] == "Resistor"), 0, absolute=0)
    sim.check("type:Resistor keeps the potentiometer", sum(1 for s in src if s["part"] == "P1"), 2, absolute=0)
    sim.check("type:Resistor keeps D1", sum(1 for s in src if s["part"] == "D1"), 1, absolute=0)

    src, n = sources(sim, "type:Resistr")
    sim.check("typo removes nothing", sum(1 for s in src if s["type"] == "Resistor"), 3, absolute=0)
    sim.check("typo reported", 1 if n.get("unmatched") == ["type:Resistr"] else 0, 1, absolute=0)
    sim.cli("sim", "stop")


if __name__ == "__main__":
    run_test(test)
