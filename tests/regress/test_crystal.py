"""Quartz crystal: equivalent circuit, high Q kept, oscillator start-up (AoE 7.38, Scherz 10.20-10.22).

SimulIDE had no crystal; with its backward-Euler L and C a motional branch of 18 fF and 88 mH
would be damped by the integration itself (about reactStep / C1 = 280 kOhm of numerical
resistance at 5 ns). The Crystal integrates its own L1-C1-R1 || C0 with the trapezoidal rule,
L1 pre-warped so the discrete series resonance is the set frequency (docs/CRYSTAL.md).

Model "4 MHz": fs 4 MHz, C1 18 fF, R1 50 ohm, C0 5 pF, so by hand
L1 = 1 / ((2 pi fs)^2 C1) = 87.96 mH, Q = 2 pi fs L1 / R1 = 44 200,
fp = fs sqrt(1 + C1/C0) = 4.007196 MHz.
- Driven at fs through 50 ohm (1 V sine): the crystal is R1 (C0's 8 kOhm in parallel barely
  matters): amplitude 0.5 V across it, within 4 %.
- Ring-down: 1 mA motional current at the start, crystal across 10 MOhm: it rings at fp
  (within 300 Hz; fs is 7.2 kHz away) and decays with tau = 2 L1 / (R1 + Rx), where 10 MOhm
  across C0 adds Rx = X0^2 / 10 MOhm = 6.3 ohm (X0 = 1/(2 pi fp C0)): amplitude ratio over
  1 ms = exp(-1 ms / tau) = 0.726, within 2 % (numerical damping would show as a lower ratio).
- Pierce oscillator: an inverting transconductance (op amp, gain 100 through 1 kOhm: 0.1 S),
  33 pF from each crystal end to ground: the motional current grows from 50 uA.
- Model: "10 MHz" survives save + reopen; R1 set by hand makes it Custom.
"""

import math
import os
import tempfile
from harness import run_test

FS, C1, R1, C0 = 4e6, 18e-15, 50.0, 5e-12
L1 = 1 / ((2 * math.pi * FS) ** 2 * C1)
FP = FS * math.sqrt(1 + C1 / C0)


def within(sim, name, value, lo, hi):
    sim.check("%s = %.7g in [%.7g, %.7g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def amplitude(sim, pin, f, n=60):
    lo, hi = 1e9, -1e9
    for i in range(n):
        sim.cli("sim", "run", "--for", "%gns" % (1 / f / n * 1e9 * 1.37))
        v = sim.cli("probe", pin)[0]["voltage"]
        lo, hi = min(lo, v), max(hi, v)
    return (hi - lo) / 2


def rising_zero(sim, pin, dt):
    """Run in dt steps until pin crosses 0 going up -> time from the call (s), interpolated."""
    t, last = 0.0, sim.cli("probe", pin)[0]["voltage"]
    while t < 1e-5:
        sim.cli("sim", "run", "--for", "%.4fns" % (dt * 1e9))
        t += dt
        v = sim.cli("probe", pin)[0]["voltage"]
        if last < 0 <= v:
            return t - dt * v / (v - last)
        last = v
    return None


def test(sim):
    c = sim.cli
    try:
        c("circuit", "new", "--discard")
        c("comp", "add", "Crystal", "--label", "X", "--at", "0,0")
    except Exception as e:
        sim.check("Crystal part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    c("sim", "start")
    r = c("read", "X")
    c("sim", "stop")
    within(sim, "L1 (mH)", r["l1"] * 1e3, L1 * 1e3 * 0.999, L1 * 1e3 * 1.001)
    within(sim, "Q", r["q"], 44000, 44400)

    # Driven at fs through 50 ohm
    c("circuit", "new", "--discard")
    c("settings", "set", "reactStep=5ns")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,-80", "Wave_Type=Sine", "Freq=4 MHz",
      "Mid_Volt=0", "Semi_Ampli=1", "Always_On=true")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "-100,0", "Resistance=50")
    c("comp", "add", "Crystal", "--label", "X", "--at", "0,0")
    c("comp", "add", "Ground", "--label", "G", "--at", "60,80")
    for a, b in (("W.outnod", "RS.lPin"), ("RS.rPin", "X.lPin"), ("X.rPin", "G.Gnd")):
        c("wire", "add", a, b)
    c("sim", "run", "--for", "12ms", timeout=900)       # 3.4 tau of the loaded crystal (1.76 ms)
    v = amplitude(sim, "X.lPin", FS)
    c("sim", "stop")
    within(sim, "driven at fs through 50 ohm: amplitude (V)", v, 0.48, 0.52)

    # Ring-down across 10 MOhm
    c("circuit", "new", "--discard")
    c("comp", "add", "Crystal", "--label", "X", "--at", "0,0", "Start_Current=1 mA", "Steps=100")
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,60", "--rot", "90", "Resistance=1e7")
    c("comp", "add", "Ground", "--label", "G", "--at", "40,120")
    for a, b in (("X.lPin", "R.lPin"), ("R.rPin", "G.Gnd"), ("X.rPin", "G.Gnd")):
        c("wire", "add", a, b)
    c("sim", "run", "--for", "20us")
    h = 1 / (FS * 100)
    rising_zero(sim, "X.lPin", h)
    n = 400
    c("sim", "run", "--for", "%.3fns" % ((n - 0.5) / FP * 1e9))
    t2 = rising_zero(sim, "X.lPin", h)
    f = n / ((n - 0.5) / FP + t2)
    a1 = amplitude(sim, "X.lPin", FP)
    c("sim", "run", "--for", "1ms", timeout=600)
    a2 = amplitude(sim, "X.lPin", FP)
    c("sim", "stop")
    within(sim, "ring-down frequency (Hz), fp = %.0f" % FP, f, FP - 300, FP + 300)
    x0 = 1 / (2 * math.pi * FP * C0)
    tau = 2 * L1 / (R1 + x0 * x0 / 1e7)
    want = math.exp(-(1e-3 + 1.37 / FP) / tau)
    within(sim, "ring-down over 1 ms (ratio), hand %.4f" % want, a2 / a1, want * 0.98, want * 1.02)

    # Pierce oscillator with an op-amp transconductance
    c("circuit", "new", "--discard")
    c("settings", "set", "reactStep=5ns")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,200")
    c("comp", "add", "OpAmp", "--label", "U", "--at", "0,0", "Volt_Pos=5", "Volt_Neg=-5", "Gain=100",
      "Out_Imped=1000")
    c("comp", "add", "Crystal", "--label", "X", "--at", "0,-100", "Start_Current=0.05 mA")
    c("comp", "add", "Capacitor", "--label", "CA", "--at", "-80,80", "--rot", "90", "Capacitance=33 pF")
    c("comp", "add", "Capacitor", "--label", "CB", "--at", "80,80", "--rot", "90", "Capacitance=33 pF")
    for a, b in (("U.inputNinv", "G.Gnd"), ("X.lPin", "U.inputInv"), ("X.rPin", "U.output"),
                 ("CA.lPin", "U.inputInv"), ("CA.rPin", "G.Gnd"), ("CB.lPin", "U.output"), ("CB.rPin", "G.Gnd")):
        c("wire", "add", a, b)

    def im_peak():
        m = 0
        for i in range(30):
            c("sim", "run", "--for", "8.3ns")
            m = max(m, abs(c("read", "X")["i_motional"]))
        return m
    c("sim", "run", "--for", "20us")
    i1 = im_peak()
    c("sim", "run", "--for", "300us", timeout=600)
    i2 = im_peak()
    c("sim", "stop")
    sim.check("Pierce: motional current grows (%.1f -> %.1f uA)" % (i1 * 1e6, i2 * 1e6), int(i2 > 1.2 * i1), 1,
              absolute=0)

    c("circuit", "new", "--discard")
    c("comp", "add", "Crystal", "--label", "X", "--at", "0,0", "Model=10 MHz")
    p = os.path.join(tempfile.mkdtemp(prefix="xtal-"), "x.sim1")
    c("circuit", "save", p)
    c("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in c("comp", "get", "X")["properties"]}
    sim.check("reopened Model (%s)" % props.get("Model"), int(props.get("Model") == "10 MHz"), 1, absolute=0)
    c("comp", "set", "X", "R1=30")
    props = {q["name"]: q["value"] for q in c("comp", "get", "X")["properties"]}
    sim.check("a value set by hand -> Custom (%s)" % props.get("Model"), int(props.get("Model") == "Custom"), 1,
              absolute=0)


run_test(test)
