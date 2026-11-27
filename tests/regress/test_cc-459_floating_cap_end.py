"""A capacitor end with no DC path follows the driven end (create-circuit #459).

A JFET mixer's output capacitor (Scherz fig 4.85) read 6.71, 6.08, 5.23 V on its open end over
three runs, with 2.9x the drain's p-p. Not reproduced now (an unconnected pin reads no voltage;
a Probe on the open end follows the drain): kept as a guard.

Circuit: sine 5 V +- 1 V, 1 kHz, through 1k into A; 1 uF from A to B; a Probe on B (no other
path). The capacitor starts uncharged and nothing can charge it but the Probe's input, so
V(B) = V(A) at every sample; two runs give the same values; the open pin of a lone capacitor
reads no voltage.
"""
from harness import run_test


def run(c):
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,0", "Wave_Type=Sine", "Freq=1kHz",
      "Semi_Ampli=1V", "Mid_Volt=5V", "Always_On=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "-100,0", "Resistance=1k")
    c("comp", "add", "Capacitor", "--label", "C", "--at", "0,0", "Capacitance=1uF")
    c("comp", "add", "Probe", "--label", "P", "--at", "100,60")
    c("comp", "add", "Capacitor", "--label", "C2", "--at", "0,120", "Capacitance=1uF")
    c("wire", "add", "W.outnod", "R.lPin")
    c("wire", "add", "R.rPin", "C.lPin")
    c("wire", "add", "C.rPin", "P.inpin")
    c("wire", "add", "C2.lPin", "R.rPin")
    c("sim", "run", "--for", "5ms")
    a, b = [], []
    for _ in range(20):
        c("sim", "run", "--for", "73us")
        p = c("probe", "C.lPin", "C.rPin")
        a.append(p[0]["voltage"]); b.append(p[1]["voltage"])
    lone = c("probe", "C2.rPin")[0]["voltage"]
    c("sim", "stop")
    return a, b, lone


def test(sim):
    a, b, lone = run(sim.cli)
    sim.check("open end follows the driven end (V, worst)", max(abs(x - y) for x, y in zip(a, b)), 0, absolute=1e-3)
    sim.check("driven end swings (p-p V)", max(a) - min(a), 2, absolute=0.3)
    a2, b2, _ = run(sim.cli)
    sim.check("second run: same open-end values (V, worst)", max(abs(x - y) for x, y in zip(b, b2)), 0, absolute=1e-6)
    sim.check("lone capacitor's open pin: no voltage", 1 if lone is None else 0, 1, absolute=0, note=str(lone))


run_test(test)
