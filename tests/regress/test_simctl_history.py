"""Keyframes (specs/sim-control.md §4.5, §4.7): a restored keyframe replays bit for bit, cheaply.

led_driver board_mains (floating_pair/board_mains.sim1, frozen copy with its MT7833A: 230 V mains,
fuse, varistor, bridge, the buck with three AngelScript cores, LED strip, scope): every one of its
27 element classes saves its state (sim history: usable, nothing missing).

- sim history check goes back to a keyframe (here 40 ms and 200 ms before now, 5 and 10 mains
  half-cycles), replays to 5 checkpoints and compares there, with the first pass, the bits of every
  node voltage AND the full saved state of every element: all identical.
- Negative control (--keep Diode: the diodes keep their present state instead of the keyframe's):
  the node voltages differ, so the check does see state that was not restored.
- Cost: a keyframe is taken every frame in well under 1 % of the 50 ms frame (measured ~15 µs);
  the speed with history on vs off is measured by the proof script, not here (wall-clock noise).
- A plain RC is checked too (the smallest case).
"""
import os
import time

from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))


def check(sim, label, span, back):
    r = sim.cli("sim", "history", "check", "--span", span, "--points", "5", "--back", back, timeout=900)
    sim.check(label + ": node voltages (5 checkpoints)", r["same_nodes"], 5, absolute=0, note="keyframe " + r["keyframe"])
    sim.check(label + ": full state (5 checkpoints)", r["same"], 5, absolute=0, note=str(r.get("differ", "")))
    return r


def test(sim):
    c = sim.cli
    c("circuit", "open", os.path.join(HERE, "floating_pair", "board_mains.sim1"), "--discard")
    c("sim", "speed", "1000000")
    c("settings", "set", "history", "true")
    c("sim", "start")
    time.sleep(3)
    h = c("sim", "history")
    sim.check("board_mains: history usable (no class missing)", 1 if h["usable"] else 0, 1, absolute=0,
              note=str(h.get("missing", "")))
    sim.check("board_mains: keyframes taken", 1 if h["keyframes"] > 20 else 0, 1, absolute=0, note=str(h["keyframes"]))
    sim.check("board_mains: capture < 500 us (1 % of a frame)", 1 if h["capture_us_mean"] < 500 else 0, 1, absolute=0,
              note="mean %.1f us, max %.1f us, %d B" % (h["capture_us_mean"], h["capture_us_max"], h["last_size"]))

    check(sim, "board_mains back 40 ms", "20ms", "40ms")
    c("sim", "resume")
    time.sleep(5)
    check(sim, "board_mains back 200 ms", "50ms", "200ms")

    c("sim", "resume")
    time.sleep(1)
    r = c("sim", "history", "check", "--span", "20ms", "--points", "4", "--back", "40ms", "--keep", "Diode", timeout=900)
    sim.check("negative control (--keep Diode): node voltages differ", 1 if r["same_nodes"] < 4 else 0, 1, absolute=0,
              note="%d of 4 the same" % r["same_nodes"])
    c("sim", "stop")

    # The smallest case: battery, R, C
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=10")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "160,0", "Capacitance=1e-6")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "C1.lPin")
    c("wire", "add", "C1.rPin", "G.Gnd")
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("settings", "set", "history", "true")
    c("sim", "speed", "0.1")
    c("sim", "start")
    time.sleep(1)
    check(sim, "RC back 0.5 ms", "1ms", "500us")


if __name__ == "__main__":
    run_test(test)
