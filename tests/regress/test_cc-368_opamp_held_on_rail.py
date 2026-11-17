"""An op amp in a loop with no static solution stays on its rails (create-circuit #368).

Scherz fig 8.42 crowbar (test_cc-368_crowbar.sim1, the book build): an op-amp comparator
(Power_Pins from the protected 6 V rail and 0 V) fires an SCR across the supply when the
supply rises past ~6.06 V. With Gain 1000, a 7 V surge made a relaxation inside one time
step: the SCR fires, the supply collapses, the comparator's ideal output swings to
-2500 V, that pulls the SCR off, the supply comes back, +400 V fires it again... The op amp
changed region (LIN / SAT+ / SAT-) until the solver's iteration budget ran out, then the
step was taken with the op amp in LIN far beyond its rails: outputs of -2542..+408 V, and
a headless run once froze for 7 minutes at 100 % CPU.

Right result: the op-amp output never leaves its supply pins (powerNeg - 0.1 V .. powerPos
+ 0.1 V); the crowbar fires and stays latched (the book: until the power is removed), the
output collapsed below 1.5 V; each 20 ms run returns within the CLI timeout.
"""
import os
from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))


def test(sim):
    c = sim.cli
    c("circuit", "open", os.path.join(HERE, "test_cc-368_crowbar.sim1"))
    c("comp", "set", "U1", "Gain=1000")
    sim.run("20ms")
    for v in ("7", "6", "7.5"):
        c("comp", "set", "V1", "Voltage=" + v)
        sim.run("20ms")                 # the harness timeout catches a frozen solver
        u = c("read", "U1")["pins"]
        out, lo, hi = u["output"], u["powerNeg"], u["powerPos"]
        sim.check("V1 %s V: op-amp output %.4g V within its supply pins %.3g..%.3g V" % (v, out, lo, hi),
                  int(lo - 0.1 <= out <= hi + 0.1), 1, absolute=0)
        q = c("read", "Q1").get("state")
        rl = abs(sim.read("RL")["voltage"])
        sim.check("V1 %s V: crowbar latched (%s, output %.3g V)" % (v, q, rl), int(q == "ON" and rl < 1.5), 1, absolute=0)
    sim.finite("op amp", "U1")


if __name__ == "__main__":
    run_test(test)
