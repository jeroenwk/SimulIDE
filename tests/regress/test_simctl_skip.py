"""Simulation control phase 1 (specs/sim-control.md §3.3–3.6): skip, step, skip interval.

- A skip lands exactly on start + span and pauses (§3.4); from a stopped simulation it starts
  it (D4). A run starts at t = 1 ps (Simulator::resetSim), so a skip of 1 ms from stopped
  lands on 1 ms + 1 ps, as sim run --for does; --count N skips N times as far (D5).
- A skip runs as fast as the computer can (D7): 10 ms of an RC at a set speed of 1 µs/s
  (which would take 10 000 s of wall time at that speed) must come back within seconds.
- A skip gives the same circuit state as sim run --for over the same span: fast run changes
  how quickly the worker runs, not what it computes. RC 1 kΩ · 1 µF (τ = 1 ms) from 10 V:
  v_C(2 ms) = 10·(1 − e⁻²) = 8.647 V; the two must agree bit for bit, and with the formula
  within the reactive step's error.
- sim step lands on the next event time, paused.
- skipInterval is a circuit setting: saved and read back (D6).
- Keys (§3.1–3.2), sent with ui type --window: Space starts / pauses / resumes, ↑ ↓ step the
  1–2–5 ladder (⇧ ×5, ⌥ ×10, ⇧⌥ ×100), → skips (⇧ 5×), Esc stops. With the canvas focused,
  Space is the keyboard editor's (D1); Esc on an idle canvas hands the keys back (D11).
- A pause writes the old info pane's numbers as one line in Messages ("Paused at … · set …").
"""

import os
import tempfile
import time

from harness import run_test


def build(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=10")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    sim.cli("comp", "add", "Capacitor", "--label", "C1", "--at", "160,0", "Capacitance=1e-6")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("wire", "add", "B1.lPin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "C1.lPin")
    sim.cli("wire", "add", "C1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")


def ps(r):
    return int(r["time_ps"])


def test(sim):
    build(sim)

    # Default interval, then set and read back
    sim.check("default skipInterval is 1 ms", 1 if sim.cli("settings", "get", "skipInterval") == "1 ms" else 0, 1, absolute=0)
    sim.cli("settings", "set", "skipInterval", "5us")

    # From stopped: starts at 0, lands on the span exactly, paused
    r = sim.cli("sim", "skip", "1ms")
    sim.check("skip 1 ms from stopped: time", ps(r), 1000000001, absolute=0)
    sim.check("skip 1 ms from stopped: paused", 1 if r["state"] == "paused" else 0, 1, absolute=0)

    # Skip interval (5 µs), then --count 3
    r = sim.cli("sim", "skip")
    sim.check("skip by the interval 5 us", ps(r), 1005000001, absolute=0)
    r = sim.cli("sim", "skip", "--count", "3")
    sim.check("skip --count 3 (15 us)", ps(r), 1020000001, absolute=0)

    # One solver step: to the next event, later than now, paused
    r = sim.cli("sim", "step")
    t = ps(r)
    sim.check("sim step advances", 1 if t > 1020000001 else 0, 1, absolute=0, note="t = %d ps" % t)
    sim.check("sim step: at most one reactive step (1 us)", 1 if t - 1020000001 <= 1000000 else 0, 1, absolute=0)
    sim.check("sim step: paused", 1 if r["state"] == "paused" else 0, 1, absolute=0)

    # Same state as sim run --for over the same span
    sim.cli("sim", "stop")
    sim.cli("sim", "skip", "2ms")
    v_skip = sim.read("C1")["voltage"]
    sim.cli("sim", "stop")
    sim.run("2ms")
    v_run = sim.read("C1")["voltage"]
    sim.check("skip and run --for agree (V)", abs(v_skip - v_run), 0, absolute=0, note="%r vs %r" % (v_skip, v_run))
    sim.check("v_C(2 ms) = 10(1-e^-2)", abs(v_skip), 8.647, tol=0.01)

    # As fast as it can: 10 ms at a set speed of 1 us/s
    sim.cli("sim", "stop")
    sim.cli("sim", "speed", "0.0001")
    t0 = time.time()
    r = sim.cli("sim", "skip", "10ms", timeout=60)
    wall = time.time() - t0
    sim.check("1 us/s sets the skip interval to 100 ns", 1 if sim.cli("settings", "get", "skipInterval") == "100 ns" else 0, 1, absolute=0,
              note=sim.cli("settings", "get", "skipInterval"))
    sim.check("skip ignores the set speed: lands", ps(r), 10000000001, absolute=0)
    sim.check("skip ignores the set speed: wall time < 10 s", 1 if wall < 10 else 0, 1, absolute=0, note="%.2f s" % wall)
    sim.cli("sim", "stop")
    sim.cli("sim", "speed", "100")
    sim.check("1 s/s sets the skip interval to 100 ms", 1 if sim.cli("settings", "get", "skipInterval") == "100 ms" else 0, 1, absolute=0)
    sim.cli("settings", "set", "skipInterval", "step")
    sim.cli("sim", "speed", "10")
    sim.check("a speed change keeps 1 step", 1 if sim.cli("settings", "get", "skipInterval") == "1 step" else 0, 1, absolute=0)
    sim.cli("sim", "speed", "100")

    # Saved with the circuit
    sim.cli("settings", "set", "skipInterval", "step")
    path = os.path.join(tempfile.mkdtemp(), "skip.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("settings", "set", "skipInterval", "1ms")
    sim.cli("circuit", "open", path, "--discard")
    sim.check("skipInterval saved (1 step)", 1 if sim.cli("settings", "get", "skipInterval") == "1 step" else 0, 1, absolute=0)


def wait_paused(sim, timeout=10):
    t0 = time.time()
    while time.time() - t0 < timeout:
        r = sim.cli("sim", "status")
        if r["state"] == "paused" and not r.get("skipping"):
            return r
        time.sleep(0.05)
    return sim.cli("sim", "status")


def test_keys(sim):
    build(sim)
    sim.cli("ui", "focus", "none")

    def key(k):
        sim.cli("ui", "type", k, "--window")
        return sim.cli("sim", "status")["state"]

    sim.check("Space starts", 1 if key("Space") == "running" else 0, 1, absolute=0)
    sim.check("Space pauses", 1 if key("Space") == "paused" else 0, 1, absolute=0)
    m = sim.cli("messages")
    lines = m["lines"] if isinstance(m, dict) else m
    sim.check("pause writes the run line in Messages", 1 if any(l.startswith("Paused at ") and " · set " in l for l in lines) else 0, 1, absolute=0)
    sim.check("Space resumes", 1 if key("Space") == "running" else 0, 1, absolute=0)
    key("Space")
    for k, want in [("Up", "2 ms"), ("shift+Up", "10 ms"), ("opt+Up", "100 ms"),
                    ("shift+opt+Down", "1 ms"), ("Down", "500 µs")]:
        key(k)
        got = sim.cli("settings", "get", "skipInterval")
        sim.check("%s -> %s" % (k, want), 1 if got == want else 0, 1, absolute=0, note=got)

    sim.cli("settings", "set", "skipInterval", "1ms")
    t0 = int(sim.cli("sim", "status")["time_ps"])
    key("Right")
    r = wait_paused(sim)
    sim.check("Right skips 1 ms", int(r["time_ps"]) - t0, 1000000000, absolute=0)
    key("shift+Right")
    r = wait_paused(sim)
    sim.check("Shift+Right skips 5 ms", int(r["time_ps"]) - t0, 6000000000, absolute=0)

    sim.check("Esc stops", 1 if key("Esc") == "stopped" else 0, 1, absolute=0)
    key("Right")
    r = wait_paused(sim)
    sim.check("Right from stopped: 1 ms, paused", int(r["time_ps"]), 1000000001, absolute=0)

    key("Space")                                   # Running
    sim.cli("ui", "focus", "canvas")
    sim.check("canvas focused: Space is not ours", 1 if key("Space") == "running" else 0, 1, absolute=0)
    sim.cli("ui", "type", "Esc")                   # On the canvas, nothing to cancel
    sim.check("Esc on the idle canvas does not stop", 1 if sim.cli("sim", "status")["state"] == "running" else 0, 1, absolute=0)
    sim.check("then Space pauses again", 1 if key("Space") == "paused" else 0, 1, absolute=0)

    # Only a click gives the canvas the keys; a click elsewhere takes them back (§3.1)
    for w in ("toolbar", "messages"):
        sim.cli("ui", "focus", "canvas")
        st = sim.cli("ui", "click", "--widget", w)
        sim.check("a click on the %s takes the focus from the canvas" % w, 1 if not st["focused"] else 0, 1, absolute=0)
        sim.check("then Space is the simulation's again (%s)" % w, 1 if key("Space") == "running" else 0, 1, absolute=0)
        key("Space")


def test_all(sim):
    test(sim)
    test_keys(sim)


if __name__ == "__main__":
    run_test(test_all)
