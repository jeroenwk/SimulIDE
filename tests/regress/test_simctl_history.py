"""Keyframes (specs/sim-control.md §4.5, §4.7): a restored keyframe replays bit for bit, cheaply.

led_driver board_mains (floating_pair/board_mains.sim1, frozen copy with its MT7833A: 230 V mains,
fuse, varistor, bridge, the buck with three AngelScript cores, LED strip, scope): every one of its
27 element classes saves its state (sim history: usable, nothing missing).

- sim history check goes back to a keyframe (here 40 ms and 200 ms before now, 5 and 10 mains
  half-cycles), replays to 5 checkpoints and compares there, with the first pass, the bits of every
  node voltage AND the full saved state of every element: all identical.
- Negative control (--keep ScriptCpu: the MT7833A's three script cores keep their present state,
  PWM phase and timers, instead of the keyframe's): the node voltages differ, so the check does
  see state that was not restored. (Not the diodes: their fields are recomputed from the voltages
  at each step, so at some moments of the mains cycle a kept diode state changes nothing.)
- Cost: a keyframe is taken every frame in well under 1 % of the 50 ms frame (measured ~15 µs);
  the speed with history on vs off is measured by the proof script, not here (wall-clock noise).
- A plain RC is checked too (the smallest case).
- Going back (sim skip --back, sim step --back, the ← key): lands exactly on the earlier time
  with the same state (sim hash) as when it was there; forward again gives the same state as the
  first time; single solver steps back land on the earlier step times. A switch clicked while
  paused at t0: back to t0 + 1 ms gives v_C = 10(1 − e⁻¹) (the click replayed), back to before t0
  gives the switch open, its button too, and v_C = 0; the scope has no sample after the
  restored time.
- A circuit with a part that has no state code yet (a BJT): going back is refused, naming it.
"""
import os
import time

from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))


def check(sim, label, span, back):
    r = sim.cli("sim", "history", "check", "--span", span, "--points", "5", "--ago", back, timeout=900)
    sim.check(label + ": node voltages (5 checkpoints)", r["same_nodes"], 5, absolute=0, note="keyframe " + r["keyframe"])
    sim.check(label + ": full state (5 checkpoints)", r["same"], 5, absolute=0, note=str(r.get("differ", "")))
    return r


def prop(c, comp, name):
    for p in c("comp", "get", comp)["properties"]:
        if p["name"] == name:
            return p["value"]


def test_back_board(sim):
    c = sim.cli
    c("circuit", "open", os.path.join(HERE, "floating_pair", "board_mains.sim1"), "--discard")
    c("sim", "speed", "1000000")
    c("settings", "set", "history", "true")
    c("sim", "start")
    time.sleep(1.5)
    c("sim", "pause")
    t0 = int(c("sim", "status")["time_ps"])
    h0 = c("sim", "hash")["hash"]
    c("sim", "skip", "10ms", timeout=300)
    h1 = c("sim", "hash")["hash"]
    r = c("sim", "skip", "10ms", "--back", timeout=300)
    sim.check("back 10 ms: lands on the start", int(r["time_ps"]), t0, absolute=0)
    sim.check("back 10 ms: the state it had there", 1 if c("sim", "hash")["hash"] == h0 else 0, 1, absolute=0)
    c("sim", "skip", "10ms", timeout=300)
    sim.check("forward again: the same state as the first time", 1 if c("sim", "hash")["hash"] == h1 else 0, 1, absolute=0)
    ts, hs = [], []
    for _ in range(3):
        r = c("sim", "step")
        ts.append(int(r["time_ps"]))
        hs.append(c("sim", "hash")["hash"])
    for i in (1, 0):
        r = c("sim", "step", "--back", timeout=300)
        sim.check("step back %d: time" % i, int(r["time_ps"]), ts[i], absolute=0)
        sim.check("step back %d: state" % i, 1 if c("sim", "hash")["hash"] == hs[i] else 0, 1, absolute=0)
    c("settings", "set", "skipInterval", "1ms")
    c("ui", "focus", "none")
    tk = int(c("sim", "status")["time_ps"])
    c("ui", "type", "Left", "--window")
    t = time.time()
    while time.time() - t < 20:
        st = c("sim", "status")
        if not st.get("skipping") and int(st["time_ps"]) != tk:
            break
        time.sleep(0.05)
    sim.check("Left goes back by the skip interval (1 ms)", tk - int(c("sim", "status")["time_ps"]), 1000000000, absolute=0)
    c("sim", "stop")


def test_back_switch(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=10")
    c("comp", "add", "Switch", "--label", "S1", "--at", "80,-40")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "160,-40", "Resistance=1000")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "240,0", "Capacitance=1e-6")
    c("comp", "add", "Oscope", "--label", "O1", "--at", "320,0")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    for a, b in (("B1.lPin", "S1.pinP0"), ("S1.switch0pinN", "R1.lPin"), ("R1.rPin", "C1.lPin"), ("C1.rPin", "G.Gnd"),
                 ("B1.rPin", "G.Gnd"), ("O1.Pin0", "C1.lPin"), ("O1.PinG", "G.Gnd")):
        c("wire", "add", a, b)
    c("settings", "set", "history", "true")
    c("sim", "speed", "0.1")
    c("sim", "start")
    time.sleep(1.5)
    c("sim", "pause")
    t0 = int(c("sim", "status")["time_ps"])
    c("ui", "click", "S1")                       # Closes while paused at t0
    c("sim", "skip", "2ms")
    r = c("sim", "skip", "1ms", "--back")
    v1 = c("read", "C1")["voltage"]
    sim.check("switch closed at t0, back to t0+1 ms: v_C = 10(1-e^-1)", v1, 6.321, tol=0.01)
    d = c("scope", "data", "O1", "--max", "0")
    ch = [x for x in d["channels"] if x.get("connected")][0]
    last = ch["samples"][-1][0] if ch["samples"] else None
    now = int(r["time_ps"]) / 1e12
    sim.check("scope: no sample after the restored time", 1 if last is not None and last <= now + 1e-12 else 0, 1,
              absolute=0, note="last %s s, now %s s" % (last, now))
    r = c("sim", "skip", "1500us", "--back")
    sim.check("back before the click: at t0 - 0.5 ms", int(r["time_ps"]), t0 - 500000000, absolute=0)
    sim.check("back before the click: the button is open again", 1 if prop(c, "S1", "Toggled") == "false" else 0, 1, absolute=0)
    sim.check("back before the click: v_C = 0", abs(c("read", "C1")["voltage"]), 0, absolute=1e-6)
    c("sim", "stop")

    # A part without state code: going back is refused and named
    c("comp", "add", "BJT", "--label", "Q1", "--at", "400,0")
    c("sim", "start")
    time.sleep(0.5)
    try:
        c("sim", "skip", "100us", "--back")
        err = ""
    except Exception as e:
        err = str(e)
    sim.check("no state code (BJT): going back refused, naming it", 1 if "BJT" in err else 0, 1, absolute=0, note=err[:120])
    c("sim", "stop")


def test_all(sim):
    test(sim)
    test_back_board(sim)
    test_back_switch(sim)


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
    r = c("sim", "history", "check", "--span", "20ms", "--points", "4", "--ago", "40ms", "--keep", "ScriptCpu", timeout=900)
    sim.check("negative control (--keep ScriptCpu): node voltages differ", 1 if r["same_nodes"] < 4 else 0, 1, absolute=0,
              note="%d of 4 the same; from %s, keyframe %s" % (r["same_nodes"], r["from"], r["keyframe"]))
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
    run_test(test_all)
