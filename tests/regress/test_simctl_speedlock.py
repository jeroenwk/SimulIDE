"""Speed lock (specs/sim-control.md §3.9): hold the set speed by coarsening the reactive step.

A 50 Hz sine into an RC (1 kΩ · 1 µF) with a reactive step of 1 ns: real time needs 10⁹
capacitor steps per second, which no computer manages, so without the lock the real speed stays
far below the set 1 s/s. With the lock on (max. reactive step 100 µs) the factor doubles once
per 3 slow frames until the solver keeps up:

- the factor goes above ×1 and the real speed comes near the set speed (> 50 %);
- the capacitor still follows the sine (its amplitude is not 0: the coarser step is still a
  valid integration, only less accurate);
- sim status flags the run (note) and gives the factor;
- the lock off: the factor goes back to ×1 at the next frame;
- a skip runs at ×1 (D15): after a skip from a locked run the factor is 1;
- speedLock and speedLockMaxStep are saved with the circuit.
"""

import os
import tempfile
import time

from harness import run_test


def build(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "WaveGen", "--label", "W1", "--at", "0,0", "Always_On=true", "Freq=50")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    sim.cli("comp", "add", "Capacitor", "--label", "C1", "--at", "160,0", "Capacitance=1e-6")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("wire", "add", "W1.outnod", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "C1.lPin")
    sim.cli("wire", "add", "C1.rPin", "G.Gnd")


def status(sim):
    return sim.cli("sim", "status")


def run_for_wall(sim, secs):
    t0 = time.time()
    while time.time() - t0 < secs:
        time.sleep(0.2)
    return status(sim)


def test(sim):
    build(sim)
    sim.cli("settings", "set", "reactStep", "1ns")
    sim.cli("sim", "speed", "100")

    # Without the lock: far below real time
    sim.cli("sim", "start")
    r = run_for_wall(sim, 2.5)
    slow = r.get("realSpeed", 0)
    sim.check("no lock: below 20 % of real time", 1 if slow < 20 else 0, 1, absolute=0, note="%.2f %%" % slow)
    sim.check("no lock: no speedLock in status", 1 if "speedLock" not in r else 0, 1, absolute=0)
    sim.cli("sim", "stop")

    # With the lock
    sim.cli("settings", "set", "speedLockMaxStep", "100us")
    sim.cli("settings", "set", "speedLock", "true")
    sim.cli("sim", "start")
    r = run_for_wall(sim, 5)
    lock = r.get("speedLock", {})
    sim.check("lock: factor above 1", 1 if lock.get("scale", 1) > 1 else 0, 1, absolute=0, note=str(lock))
    sim.check("lock: real speed above 50 %", 1 if r.get("realSpeed", 0) > 50 else 0, 1, absolute=0,
              note="%.2f %%" % r.get("realSpeed", 0))
    sim.check("lock: status note", 1 if "speed lock" in r.get("note", "") else 0, 1, absolute=0, note=r.get("note", ""))
    m = sim.cli("messages")
    lines = m["lines"] if isinstance(m, dict) else m
    sim.check("lock: factor change in Messages", 1 if any(l.startswith("Speed lock at t = ") for l in lines) else 0, 1, absolute=0)

    # The capacitor still follows the sine: sample it over a period
    vs = []
    for _ in range(12):
        vs.append(sim.read("C1")["voltage"])
        time.sleep(0.003)
    sim.check("lock: the capacitor still moves", 1 if max(vs) - min(vs) > 0.01 else 0, 1, absolute=0,
              note="%.3f .. %.3f V" % (min(vs), max(vs)))

    # A skip runs at x1 (D15)
    sim.cli("sim", "skip", "1ms")
    r = status(sim)
    sim.check("skip: factor back to 1", r.get("speedLock", {}).get("scale"), 1, absolute=0)
    sim.cli("sim", "resume")

    # Lock off: x1 at the next frame
    sim.cli("settings", "set", "speedLock", "false")
    time.sleep(0.5)
    r = status(sim)
    sim.check("lock off: factor 1", r.get("speedLock", {}).get("scale", 1), 1, absolute=0)
    sim.cli("sim", "stop")

    # Saved with the circuit
    sim.cli("settings", "set", "speedLock", "true")
    path = os.path.join(tempfile.mkdtemp(), "lock.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("settings", "set", "speedLock", "false")
    sim.cli("settings", "set", "speedLockMaxStep", "0")
    sim.cli("circuit", "open", path, "--discard")
    sim.check("speedLock saved", 1 if sim.cli("settings", "get", "speedLock") is True else 0, 1, absolute=0)
    got = sim.cli("settings", "get", "speedLockMaxStep")
    sim.check("speedLockMaxStep saved (100 us)", 1 if got.replace("µ", "u").replace(" ", "") == "100us" else 0, 1,
              absolute=0, note=str(got))


if __name__ == "__main__":
    run_test(test)
