"""The first run after opening a circuit is the same as every later run (TODO first-run-diff).

A run must start from the same state whatever ran before (specs/sim-control.md §4.8). It didn't:
fields that only the load set, or that a run left behind, made run 1 differ from runs 2, 3...
(those agreed). Found with `sim hash --elements` and a start-state dump, fixed:

- eElement::m_changed was never initialized; it is now cleared at each start (parts that need it
  set it in initialize/stamp: BJT, JFET, clocks, controlled sources, and the diode, which used it
  to linearize at the first step only after a load).
- eDiode: m_voltSolved left from the last run.
- A meter's output pin kept the last run's reading (Meter::initialize), and IoPin::updateStep
  (pin colours, once per GUI frame) wrote the simulation's input state (m_inpVolt, hysteresis),
  so a run depended on when frames fell.
- IoPin::m_inpVolt, WaveGen's time and next step, the clocked devices' clock state, the MCU
  compare unit's pin level (garbage before: never initialized).
- An MCU timer's external clock level (McuTimer::enableExtClock), read when the reset writes
  OPTION during Mcu::stamp, maybe before the clock pin's own stamp: the pin still had the mode the
  load or the last run left, so the start level was random (887 CCP failed about every other
  run of the suite, depending on the circuits run before). Now low at the start, like the nodes.

Each circuit is opened, run 1 ms, stopped, run 1 ms again: the full state (every element's saved
state, nodes, event queue) must be the same bit for bit. The flip-flops draw a random start
state, so the seed is fixed (settings runSeed).
"""
import os

from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, "..", "..", "resources", "examples")

CIRCUITS = [
    ("board_mains (MT7833A script cores, bridge, meter)", os.path.join(HERE, "floating_pair", "board_mains.sim1")),
    ("precision rectifier (op amp, diode, ammeter)", os.path.join(EX, "Active", "Diode", "precision_rectifier.sim1")),
    ("triac (wave generator, ammeter)", os.path.join(EX, "Active", "triac.sim1")),
    ("SCR (ammeter output)", os.path.join(EX, "Active", "SCR.sim1")),
    ("npn curve tracer (ammeter outputs in the loop: hung when the meter stamped in initialize)", os.path.join(EX, "Active", "BJT", "npn-curve.sim1")),
    ("PIC 16F887 CCP (compare unit, JK flip-flops)", os.path.join(EX, "Micro", "Pic", "16F88x", "16f887_CCP", "887_ccp.sim1")),
]


def twice(sim, name, path):
    c = sim.cli
    c("circuit", "open", os.path.abspath(path), "--discard", timeout=60)
    c("settings", "set", "runSeed", "12345")
    hs = []
    for _ in range(2):
        c("sim", "skip", "1ms", timeout=300)
        hs.append(c("sim", "hash", "--elements"))
        c("sim", "stop")
    a, b = hs
    ea = dict(e.rsplit(":", 1) for e in a["elements"])
    eb = dict(e.rsplit(":", 1) for e in b["elements"])
    diff = [k for k in ea if ea[k] != eb.get(k)]
    sim.check("%s: node voltages, run 1 = run 2" % name, 1 if a["hash"] == b["hash"] else 0, 1, absolute=0)
    sim.check("%s: full state, run 1 = run 2" % name, 1 if a["state"] == b["state"] else 0, 1, absolute=0,
              note=", ".join(diff[:8]))


def test(sim):
    for name, path in CIRCUITS:
        twice(sim, name, path)
    sim.cli("settings", "set", "runSeed", "0")


if __name__ == "__main__":
    run_test(test)
