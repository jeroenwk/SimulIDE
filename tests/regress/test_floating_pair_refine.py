"""A floating mains pair held only by off diodes converges (iterative refinement).

led_driver board_mains (floating_pair/board_mains.sim1, a frozen copy with its MT7833A in
floating_pair/data: SimulIDE finds a subcircuit in data/ next to the circuit when its libraries
don't have it): 230 V floating source -> fuse -> 1N4007 bridge -> 100 nF -> the MT7833A buck,
a varistor across the lines. Near a zero crossing all four bridge diodes are off and the
source pair (1e3 S between its pins) hangs on ~1e-11 S of diode leakage. The LU (no pivoting)
then lost the pair's common-mode voltage in rounding: each solve put it elsewhere by ~10 mV,
the diodes never saw two solves within their 10 mV and the step at 389.5 ms took 100000
iterations ("Not converging ... Still iterating: D1, D2, ..."). The same happened with a plain
resistor of 1 MOhm .. 10 GOhm across the lines instead of the varistor.

Right result: no "Not converging" message in the first 450 ms.
"""
import os
from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))


def test(sim):
    c = sim.cli
    c("circuit", "open", os.path.join(HERE, "floating_pair", "board_mains.sim1"), "--discard")
    c("sim", "speed", "1000000")
    sim.run("450ms")
    bad = [l for l in c("messages")["lines"] if "Not converging" in l]
    sim.check("no non-convergence in 450 ms (%s)" % (bad[0][:60] if bad else "none"), len(bad), 0, absolute=0)
    sim.finite("bridge diode D1", "D1")


if __name__ == "__main__":
    run_test(test)
