"""A meter's output pin follows its reading in simulation time, not once per GUI frame.

The voltmeter / ammeter output pin (a voltage source at the reading, wired to a scope in
npn-curve, diac and Scherz fig 4.63) was set in Meter::updateStep, once per screen frame. So
the output lagged the reading by a frame, its steps fell where frames fell (a run depended on
the speed and on the machine), and a replay after going back differed (npn-curve). It is now set
by an event at the step where the reading changed (Mean/RMS: also at each block end), and only
when the pin is wired.

Checks, per circuit: the output pin equals the reading at several pauses; the same node voltages
after a fast skip and after a slow run (1 % of real time: many frames) to the same time; going
back is bit-identical. Then the same with the npn-curve meter in Mean mode.
"""
import os

from harness import run_test
from test_simctl_history_parts import roundtrip

HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, "..", "..", "resources", "examples")

CIRCUITS = [
    ("npn curve tracer", os.path.join(EX, "Active", "BJT", "npn-curve.sim1"), "Amperimeter-49"),
    ("diac", os.path.join(EX, "Active", "diac.sim1"), "Amperimeter-5"),
]


def output_pin(r):
    return [v for p, v in (r.get("pins") or {}).items() if "outnod" in p][0]


def follows(sim, name, path, meter, mean=False):
    c = sim.cli
    c("circuit", "open", os.path.abspath(path), "--discard", timeout=60)
    if mean:
        c("comp", "set", meter, "Reading=Mean")
        c("comp", "set", meter, "Avg_Time=1ms")
    worst = 0.0
    for k in range(6):
        c("sim", "skip", "%dus" % (137 + 911 * k), timeout=120)
        r = c("read", meter)
        v, o = r["value"], output_pin(r)
        worst = max(worst, abs(v - o) / max(abs(v), 1e-15))
    c("sim", "stop")
    sim.check("%s: output pin = reading (worst relative difference)" % name, worst, 0, absolute=1e-9)

    c("sim", "skip", "5ms", timeout=120)
    fast = c("sim", "hash")["hash"]
    c("sim", "stop")
    c("sim", "speed", "1")
    c("sim", "run", "--for", "5ms", timeout=120)
    slow = c("sim", "hash")["hash"]
    c("sim", "stop")
    c("sim", "speed", "100")
    sim.check("%s: same node voltages after a fast skip and a slow run" % name,
              1 if fast == slow else 0, 1, absolute=0)


def test(sim):
    sim.cli("settings", "set", "history", "true")
    for name, path, meter in CIRCUITS:
        follows(sim, name, path, meter)
        roundtrip(sim, name, path)
    name, path, meter = CIRCUITS[0]
    follows(sim, name + " (Mean, 1 ms blocks)", path, meter, mean=True)


if __name__ == "__main__":
    run_test(test)
