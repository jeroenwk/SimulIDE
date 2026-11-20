"""CLI reads while the simulation runs give a solved step, not an iteration in progress.

The simulation worker runs in its own thread. A CLI command arriving while it ran read the
circuit at once, racing the worker, and the GUI timer stopped the worker in the middle of a
step at each frame. Either way a `read` could return the values of an unfinished Newton
iteration: on led_driver board_mains (floating_pair/board_mains.sim1, also used by
test_floating_pair_refine) a bridge diode read 10.6 A where no solved step goes above 2.2 A
(`sim average` over 3 s), and the diodes' over-current flash came from the same values.

Detector: Kirchhoff's current law at the node after the fuse, where F1, D6, D8 and the
varistor meet. In a solved step I(F1) = I(D6) - I(D8) + I(Z1) to ~1e-13 A; in an iteration in
progress the diode currents belong to the previous solve and it was broken in 268 of 300 reads,
by up to 7.9 A.

Right result: every read taken while running keeps KCL (within 1 ppm + 1 nA), and no bridge
diode reads above 2.5 A.
"""
import os
from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))
SAMPLES = 150


def test(sim):
    c = sim.cli
    c("circuit", "open", os.path.join(HERE, "floating_pair", "board_mains.sim1"), "--discard")
    c("sim", "speed", "1000000")
    sim.run("380ms")                             # Up to the chip's soft start
    c("sim", "start")
    broken, worst, peak = 0, 0.0, 0.0
    for _ in range(SAMPLES):
        r = {x["uid"]: x for x in c("read", "F1", "D1", "D5", "D6", "D8", "Varistor-95")}
        i_f = r["Fuse-53"]["current"]
        res = i_f - (r["Diode-54"]["current"] - r["Diode-55"]["current"] + r["Varistor-95"]["current"])
        if abs(res) > 1e-6 * abs(i_f) + 1e-9:
            broken += 1
        worst = max(worst, abs(res))
        peak = max([peak] + [abs(r[d]["current"]) for d in ("Diode-57", "Diode-58", "Diode-54", "Diode-55")])
    t = c("sim", "status")["time"]
    c("sim", "pause")
    sim.check("reads breaking KCL at F1/D6/D8/Z1, of %d up to %s (worst %.3g A)" % (SAMPLES, t, worst),
              broken, 0, absolute=0)
    sim.check("largest bridge-diode current read <= 2.5 A (%.3g A)" % peak, int(peak <= 2.5), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
