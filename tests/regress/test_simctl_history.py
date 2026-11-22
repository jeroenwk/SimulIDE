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
- A circuit that can't go back: a warning in Messages at load and at each ←, ⏮ greyed out.
- A circuit with a part that has no exact replay (the audio output, paced by the sound card): going back is refused, naming it.
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
    c("comp", "add", "AudioOut", "--label", "SPK1", "--at", "400,0")
    c("sim", "start")
    time.sleep(0.5)
    try:
        c("sim", "skip", "100us", "--back")
        err = ""
    except Exception as e:
        err = str(e)
    sim.check("no exact replay (AudioOut): going back refused, naming it", 1 if "AudioOut" in err else 0, 1, absolute=0, note=err[:120])
    c("sim", "stop")


def test_off_warning(sim):
    """A circuit that can't go back: a warning in Messages when it loads, again at each ←, and ⏮
    greyed out with the reason in its tooltip; a circuit that can: ⏮ enabled"""
    c = sim.cli
    ex = os.path.join(HERE, "..", "..", "resources", "examples")
    def warnings():
        m = c("messages")
        lines = m["lines"] if isinstance(m, dict) else m
        return [l for l in lines if "Going back in time is off for this circuit" in l]
    def back_btn():
        return [a for a in c("ui", "toolbar") if a["text"] == "Skip Back"][0]
    c("circuit", "open", os.path.abspath(os.path.join(ex, "Outputs", "Audio", "audio.sim1")), "--discard")
    w = warnings()
    sim.check("off: warning at load, naming the part and why", 1 if w and "AudioOut" in w[-1] and "sound card" in w[-1] else 0, 1,
              absolute=0, note=(w[-1] if w else "none")[:140])
    c("sim", "start")
    time.sleep(0.3)
    b = back_btn()
    sim.check("off: ⏮ greyed out", 0 if b["enabled"] else 1, 1, absolute=0)
    sim.check("off: ⏮ tooltip says why", 1 if "AudioOut" in b["tooltip"] else 0, 1, absolute=0, note=b["tooltip"][-80:])
    n = len(warnings())
    c("ui", "focus", "none")
    c("ui", "type", "Left", "--window")
    sim.check("off: ← repeats the warning", len(warnings()) - n, 1, absolute=0)
    c("sim", "stop")
    c("circuit", "open", os.path.abspath(os.path.join(ex, "Active", "SCR.sim1")), "--discard")
    c("sim", "start")
    time.sleep(0.3)
    sim.check("on: ⏮ enabled for a circuit that can go back", 1 if back_btn()["enabled"] else 0, 1, absolute=0)
    c("sim", "stop")


def test_back_states(sim):
    """Part states go back too: board_mains' bridge diode D8 conducts from ~13.2 to ~15.4 ms. Going
    back from 16 ms to 14 ms used to leave its state log at OFF (badge, read, status), and the
    replay's OFF -> ON was then no change, so it was never reported."""
    c = sim.cli
    c("circuit", "open", os.path.join(HERE, "floating_pair", "board_mains.sim1"), "--discard")
    c("settings", "set", "history", "true")
    was = c("settings", "get", "partStatus")      # The user's settings file: put it back after
    c("settings", "set", "partStatus", "true")
    c("comp", "set", "D8", "Show_State=true")
    c("sim", "skip", "14ms", timeout=300)

    def d8():
        return c("read", "D8").get("state")

    units = {"ps": 1e-12, "ns": 1e-9, "us": 1e-6, "µs": 1e-6, "ms": 1e-3, "s": 1}

    def changes(after_ps):
        out = []
        for e in c("status", "D8")["history"]:
            v, u = e["time"].split()
            if float(v) * units[u] * 1e12 > after_ps:
                out.append(e)
        return out

    sim.check("D8 at 14 ms: ON", 1 if d8() == "ON" else 0, 1, absolute=0, note=str(d8()))
    c("sim", "skip", "2ms", timeout=300)
    sim.check("D8 at 16 ms: OFF", 1 if d8() == "OFF" else 0, 1, absolute=0, note=str(d8()))
    c("sim", "skip", "2ms", "--back", timeout=300)
    sim.check("back to 14 ms: D8 ON again", 1 if d8() == "ON" else 0, 1, absolute=0, note=str(d8()))
    sim.check("back to 14 ms: no change after 14 ms in its history", len(changes(14000000001)), 0, absolute=0,
              note=str(changes(14000000001)))
    c("sim", "skip", "2ms", timeout=300)
    ev = changes(14000000001)
    sim.check("forward again: D8 OFF", 1 if d8() == "OFF" else 0, 1, absolute=0, note=str(d8()))
    sim.check("forward again: the ON -> OFF reported once", len(ev), 1, absolute=0, note=str(ev))
    c("sim", "stop")
    c("settings", "set", "partStatus", str(was).lower())


def test_back_dots(sim):
    """Going back in time: the current-flow dots run backwards for a moment (a cue, ~0.5 s), then
    stand still (paused); running forward they go with the current."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=10")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "160,0", "Capacitance=1e-3")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    for a, b in (("B1.lPin", "R1.lPin"), ("R1.rPin", "C1.lPin"), ("C1.rPin", "G.Gnd"), ("B1.rPin", "G.Gnd")):
        c("wire", "add", a, b)
    c("settings", "set", "history", "true")
    was = c("settings", "get", "currentFlow")     # The user's settings file: put it back after
    c("settings", "set", "currentFlow", "true")
    c("sim", "speed", "1")
    c("sim", "start")
    time.sleep(1.5)

    def dots():
        return {r["uid"]: r.get("dots") for r in c("wire", "current") if abs(r.get("current", 0)) > 1e-6}

    flip = {"forward": "backward", "backward": "forward"}
    run = dots()
    sim.check("running: dots move on every wire", 1 if run and all(v in flip for v in run.values()) else 0, 1, absolute=0, note=str(run))
    c("sim", "skip", "1ms", "--back")
    back = dots()
    sim.check("just gone back: every wire's dots reversed", 1 if back and all(back.get(k) == flip.get(v) for k, v in run.items()) else 0, 1,
              absolute=0, note="%s -> %s" % (run, back))
    time.sleep(1.0)
    d = set(dots().values())
    sim.check("a second later, paused: dots still", 1 if d == {"still"} else 0, 1, absolute=0, note=str(d))
    c("sim", "stop")
    c("settings", "set", "currentFlow", str(was).lower())


def test_all(sim):
    test(sim)
    test_back_dots(sim)
    test_back_states(sim)
    test_back_board(sim)
    test_back_switch(sim)
    test_off_warning(sim)


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
