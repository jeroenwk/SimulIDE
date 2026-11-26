"""Crystal: its step must hold every reactive solve, or C0 corrupts the nodes it touches.

Found by the GLM check of AoE Fig. 7.38 panel A (JFET Pierce, 4 MHz; books/bookparts-followup.md
§1). The Crystal integrates C0 with the trapezoidal rule at its own step h = 1 / (fs x Steps):
i(n+1) = 2 C0 / h (v(n+1) - v(n)) - i(n). That companion is right only at the crystal's own step
times. A solve in between (a capacitor or JFET stepping at reactStep) sees, just after a crystal
step, i = -i(n): C0's current reversed. The motional branch (L1-dominated) keeps its current
there, so only C0 is wrong. On a high-impedance node (the Pierce gate: C0, Cgs and 10 MOhm) the
parts that step at reactStep read that wrong voltage and integrate it, every step.

With the default Steps 40 (h = 6.25 ns at 4 MHz):
- reactStep 5 ns: the gate is about 9 mV off at each crystal-only solve, on a +-20 mV swing
  (first measured with `sim step` here; the note took it for a Nyquist mode of the trapezoid).
- reactStep 1 ns (what docs/CRYSTAL.md recommends at 4 MHz): the node between the 1 nF and the
  crystal, which has no DC path, is pumped to about -7 kV.
With the crystal's step equal to reactStep (Steps 50 at 5 ns, Steps 250 at 1 ns) the gate is
smooth. So the right result: the crystal steps at reactStep / k (k a whole number, as fine as
Steps asks or finer), so every reactive solve is also a crystal step.

Checks (circuit: +10 V, RFC 2.5 mH with 10 pF, 1 nF from drain to the crystal, crystal to the
gate, 10 MOhm gate to ground, JFET Cgs 4.6 pF / Cgd 1 pF, crystal 4 MHz, Start_Current 50 uA):
- the crystal's step (read `step`) divides reactStep: 5 ns at 5 ns; 1 ns at 1 ns; 6.25 ns at
  the default 1 us (Steps 40 asks 6.25 ns and 1 us / 160 is that);
- reactStep 5 ns, 300 us in, 60 consecutive solver steps: no gate sample is off the line
  through its neighbours by more than 5 % of the gate's range over them (smooth sine at 5 ns
  steps: about 1 %; now: about 30 %);
- reactStep 1 ns, 300 us in: the drain stays between -1 V and 30 V, and the node between the
  1 nF and the crystal (no DC path: only capacitors) within +-30 V; now the off-grid error
  pumps it to about -7 kV.
"""

from harness import run_test


def build(c, react, steps=40):
    c("circuit", "new", "--discard")
    if react:
        c("settings", "set", "reactStep=%s" % react)
    c("comp", "add", "Rail", "--label", "V", "--at", "0,-160", "Voltage=10")
    c("comp", "add", "Inductor", "--label", "L", "--at", "0,-100", "--rot", "90", "Inductance=2.5 mH", "Cp=10 pF")
    c("comp", "add", "Capacitor", "--label", "CA", "--at", "-60,-40", "Capacitance=1 nF")
    c("comp", "add", "Crystal", "--label", "X", "--at", "-120,30", "--rot", "90", "Start_Current=0.05 mA",
      "Steps=%d" % steps)
    c("comp", "add", "Resistor", "--label", "R", "--at", "-120,100", "--rot", "90", "Resistance=10 MΩ")
    c("comp", "add", "Jfet", "--label", "Q", "--at", "40,30", "Cgs=4.6 pF", "Cgd=1 pF")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,200")
    for a, b in (("V.outnod", "L.lPin"), ("L.rPin", "Q.Dren"), ("L.rPin", "CA.rPin"), ("CA.lPin", "X.lPin"),
                 ("X.rPin", "Q.Gate"), ("X.rPin", "R.lPin"), ("R.rPin", "G.Gnd"), ("Q.Sour", "G.Gnd")):
        c("wire", "add", a, b)


def test(sim):
    c = sim.cli

    for react, want in (("5ns", 5000), ("1ns", 1000), (None, 6250)):
        build(c, react)
        c("sim", "start")
        step = c("read", "X").get("step")
        c("sim", "stop")
        sim.check("crystal step (ps) at reactStep %s" % (react or "1us (default)"),
                  step * 1e12 if step is not None else -1, want, tol=0.001)

    # reactStep 5 ns: the gate over 60 consecutive solves
    build(c, "5ns")
    c("sim", "skip", "300us", timeout=900)
    t, v = [], []
    for i in range(60):
        c("sim", "step")
        t.append(float(c("sim", "status")["time_ps"]))
        v.append(c("probe", "X.rPin")[0]["voltage"])
    c("sim", "stop")
    span = max(v) - min(v)
    worst = 0
    for i in range(1, len(v) - 1):
        line = v[i - 1] + (v[i + 1] - v[i - 1]) * (t[i] - t[i - 1]) / (t[i + 1] - t[i - 1])
        worst = max(worst, abs(v[i] - line))
    sim.check("reactStep 5 ns: gate off its neighbours' line %.2f mV / range %.2f mV (<= 0.05)"
              % (worst * 1e3, span * 1e3), int(span > 0 and worst <= 0.05 * span), 1, absolute=0)

    # reactStep 1 ns: no runaway
    build(c, "1ns")
    c("sim", "skip", "300us", timeout=1800)
    lo, hi, top = 1e9, -1e9, 0
    for i in range(40):
        c("sim", "run", "--for", "6.7ns")
        q = c("probe", "Q.Dren")[0]["voltage"]
        lo, hi = min(lo, q), max(hi, q)
        top = max(top, abs(c("probe", "X.lPin")[0]["voltage"]))
    c("sim", "stop")
    sim.check("reactStep 1 ns: drain within -1 .. 30 V (%.2f .. %.2f V)" % (lo, hi),
              int(-1 <= lo and hi <= 30), 1, absolute=0)
    sim.check("reactStep 1 ns: CA-crystal node within +-30 V (|v| <= %.2f V)" % top, int(top <= 30), 1, absolute=0)


run_test(test)
