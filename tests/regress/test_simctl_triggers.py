"""Circuit triggers (specs/triggers.html), step 1: voltage and current triggers from the CLI.

An RC (1 kOhm, 1 uF: tau = 1 ms) driven by a 0..10 V square wave of 50 Hz (10 ms high, 10 ms low:
10 tau, so each half period starts from a full charge or discharge). Expected times come from
the circuit, not from the simulator (step 1 us):
- rising 6.3212 V on C1 (10 (1 - 1/e)): 1 ms after a rising edge; falling 3.6788 V: 1 ms after a
  falling edge (11 ms);
- the window 2..8 V left at ln 5 tau = 1.609 ms after each edge: twice per period;
- across R1 (pin pair R1.lPin-C1.lPin): 10 V e^(-t/tau) falls through 5 V at ln 2 tau = 0.693 ms;
- current through R1 crosses 0 at every edge of the wave (cross: either way): 10 per 100 ms;
- holdoff 25 ms on a 20 ms rising event: every other one; max 3 disarms after 3; single after 1.
Run control: sim run --until-trigger and sim run --for end at a pause trigger, a skip reports
"stopped by trigger", going back removes the later hits without firing on the replay (and
re-arms a single trigger whose hit is gone), replaying forward gives the same hits. Saved:
save and reopen keep the triggers; delete and undo. Start: a signal that starts above a rising
level does not fire.
Step 2: the key frame action (at the trigger, named by it; 1 ms before a falling trigger at 11 ms:
saved at 10 ms, with the state the run passed through, bit for bit), the run going on, a key
frame saved before a trigger was added still loading without "circuit changed"; part states on
led_driver board_mains (frozen copy): supply any -> ON pauses when the state log says ON, a
block's start state does not fire "change", ">=WAIT", a wrong block name lists the blocks.
"""
import os
import tempfile

from harness import run_test

TAU = 1e-3
STEP = 1e-6      # The circuit's step: a hit is at the first solved step at or past the level


def build(c):
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "0,0", "Wave_Type=Square", "Freq=50Hz",
      "Always_On=true", "Semi_Ampli=5V", "Mid_Volt=5V")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "100,0", "Resistance=1k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "200,40", "--rotate", "90", "Capacitance=1uF")
    c("comp", "add", "Ground", "--label", "G", "--at", "100,100")
    c("wire", "add", "W.outnod", "R1.lPin")
    c("wire", "add", "R1.rPin", "C1.lPin")
    c("wire", "add", "C1.rPin", "G.Gnd")


def secs(ps):
    return int(ps) * 1e-12


def hits(c, tid):
    return [secs(t) for t in c("trigger", "hits", str(tid))[0]["times_ps"]]


def test(sim):
    c = sim.cli
    c("settings", "set", "history", "true")
    build(c)

    # --- pause, single, sim run --until-trigger
    t1 = c("trigger", "add", "C1.lPin", "rise:6.3212", "--pause")
    sim.check("add: watches", 1 if t1["watches"] == "C1 pin 1" else 0, 1, absolute=0, note=t1["watches"])
    r = c("sim", "run", "--until-trigger", "--timeout", "100ms")
    sim.check("until-trigger: fired", 1 if r.get("fired") else 0, 1, absolute=0)
    sim.check("until-trigger: paused at tau", secs(r["time_ps"]), TAU, absolute=STEP)
    sim.check("until-trigger: names the trigger", r["trigger"]["id"], t1["id"], absolute=0)
    sim.check("single: disarmed itself", 1 if not r["trigger"]["armed"] else 0, 1, absolute=0)
    msgs = c("messages")["lines"]
    sim.check("Messages: the trigger's line", 1 if any(l.startswith("Trigger: C1 pin 1 ↑ 6.3212 V at 1 ms (paused)") for l in msgs) else 0, 1, absolute=0)

    # --- count only, normal, holdoff, max, windows, pairs, currents
    c("trigger", "add", "C1", "fall:3.6788", "--pause", "--mode", "normal")   # 2
    c("trigger", "add", "C1", "out:2..8", "--max", "3")                        # 3
    c("trigger", "add", "R1:i", "cross:0", "--mode", "normal")                 # 4
    c("trigger", "add", "R1.lPin-C1.lPin", "fall:5", "--mode", "normal")       # 5
    c("trigger", "add", "C1.1", "rise:5", "--holdoff", "25ms")                 # 6
    c("trigger", "add", "C1.lPin", "rise:-1", "--mode", "normal")              # 7: starts above
    c("trigger", "add", "R1.lPin:i", "rise:5mA", "--mode", "normal")           # 8: = R1:i
    r = c("sim", "run", "--for", "20ms")
    sim.check("run --for: ended early by the falling trigger", secs(r["time_ps"]), 10e-3 + TAU, absolute=STEP)
    sim.check("run --for: says which", r.get("trigger", {}).get("id", 0), 2, absolute=0)
    c("trigger", "disarm", "2")
    c("sim", "run", "--for", "89ms")              # To 100 ms
    h3, h4, h5, h6, h7, h8 = (hits(c, i) for i in (3, 4, 5, 6, 7, 8))
    sim.check("window: 3 hits (max 3)", len(h3), 3, absolute=0)
    for k, t in enumerate(h3):
        sim.check("window: hit %d at ln5 tau after an edge" % (k + 1), t, 10e-3 * k + 1.6094 * TAU, absolute=STEP)
    sim.check("window: disarmed at max", 1 if not c("trigger", "list")[2]["armed"] else 0, 1, absolute=0)
    sim.check("current cross 0: one per edge", len(h4), 10, absolute=0)
    sim.check("current cross 0: first at 10 ms", h4[0] if h4 else 0, 10e-3, absolute=STEP)
    # 5 and 6 were added at 1 ms: from the rising edge at 20 ms on (the run ends at 100 ms)
    sim.check("pin pair across R1: once per rising edge", len(h5), 4, absolute=0)
    sim.check("pin pair: ln2 tau after the edge", h5[0] if h5 else 0, 20e-3 + 0.6931 * TAU, absolute=STEP)
    sim.check("holdoff 25 ms: every other rising edge", len(h6), 2, absolute=0)
    sim.check("holdoff: the second at 60 ms + ln2 tau", h6[1] if len(h6) > 1 else 0, 60e-3 + 0.6931 * TAU, absolute=STEP)
    sim.check("start above a rising level: no hit", len(h7), 0, absolute=0)
    sim.check("pin current = two-pin current", len(h8), 5, absolute=0)
    msgs = c("messages")["lines"]
    sim.check("count only: a summary line at the pause",
              1 if any(l.startswith("Trigger: R1 current (1→2) ↕ 0 A: 10 hits") for l in msgs) else 0, 1, absolute=0)

    # --- going back: later hits removed, no firing on the replay, the same hits forward again
    before = hits(c, 4)
    r = c("sim", "skip", "45ms", "--back")
    t = secs(r["time_ps"])
    sim.check("back: at 55 ms", t, 55e-3, absolute=STEP)
    sim.check("back: hits after 55 ms removed, none from the replay", len(hits(c, 4)), 5, absolute=0)
    c("sim", "run", "--for", "45ms")
    sim.check("forward again: the same hits", 1 if hits(c, 4) == before else 0, 1, absolute=0,
              note="%s vs %s" % (hits(c, 4), before))

    # --- edits during a run apply at the next frame; going back before a single trigger's hit re-arms it
    t9 = c("trigger", "add", "C1", "rise:9", "--pause")   # 10 (1-e^-t) = 9 at ln10 tau after the edge at 100 ms
    r = c("sim", "run", "--for", "10ms")
    sim.check("added while paused: fires", secs(r["time_ps"]), 100e-3 + 2.3026 * TAU, absolute=STEP)
    c("sim", "skip", "5ms", "--back")
    t = [x for x in c("trigger", "list") if x["id"] == t9["id"]][0]
    sim.check("back before a single hit: re-armed, no hit", 1 if t["armed"] and t["hits"] == 0 else 0, 1, absolute=0, note=str(t))
    r = c("sim", "skip", "10ms")
    sim.check("skip: stopped by the trigger", 1 if "stopped by trigger" in r.get("note", "") else 0, 1, absolute=0,
              note=r.get("note", ""))
    sim.check("skip: at its step", secs(r["time_ps"]), 100e-3 + 2.3026 * TAU, absolute=STEP)
    c("sim", "stop")

    # --- saved with the circuit; undo
    path = os.path.join(tempfile.mkdtemp(prefix="simulide-triggers-"), "rc.sim1")
    c("circuit", "save", path)
    saved = [(t["id"], t["watches"], t["condition"], t["action"], t["mode"], t["armed"]) for t in c("trigger", "list")]
    c("circuit", "open", path, "--discard")
    loaded = [(t["id"], t["watches"], t["condition"], t["action"], t["mode"], t["armed"]) for t in c("trigger", "list")]
    sim.check("save + open: the same triggers", 1 if saved == loaded else 0, 1, absolute=0, note="%s" % loaded)
    c("trigger", "del", "1", "2")
    sim.check("del: two fewer", len(c("trigger", "list")), len(saved) - 2, absolute=0)
    c("undo")
    sim.check("undo: back", len(c("trigger", "list")), len(saved), absolute=0)
    c("redo")
    sim.check("redo: gone again", len(c("trigger", "list")), len(saved) - 2, absolute=0)

    keyframes(sim, os.path.dirname(path))
    states(sim)


def keyframes(sim, folder):
    """Step 2: the key frame action, at the trigger and before it; trigger edits and key frames."""
    c = sim.cli
    build(c)
    path = os.path.join(folder, "rc_kf.sim1")
    c("circuit", "save", path)
    c("sim", "speed", "10")                       # 5 ms per frame: the history holds the 10 ms before
    c("trigger", "add", "C1", "rise:6.3212", "--keyframe", "--name", "at tau")       # 1: at 1 ms
    c("trigger", "add", "C1", "fall:3.6788", "--keyframe", "--before", "1ms")        # 2: at 11 ms, saved at 10 ms
    r = c("sim", "run", "--for", "15ms", timeout=300)
    sim.check("key frame action: the run goes on", secs(r["time_ps"]), 15e-3, absolute=STEP)
    kfs = c("sim", "keyframe", "list")["keyframes"]
    sim.log("key frames: %s" % [(k["n"], k["time"], k["note"]) for k in kfs])
    sim.check("two key frames saved", len(kfs), 2, absolute=0)
    if len(kfs) == 2:
        sim.check("at the trigger: its time", secs(kfs[0]["time_ps"]), TAU, absolute=STEP)
        sim.check("at the trigger: named by the trigger", 1 if kfs[0]["note"] == "at tau" else 0, 1, absolute=0, note=kfs[0]["note"])
        sim.check("before: 1 ms before the falling trigger", secs(kfs[1]["time_ps"]), 10e-3, absolute=STEP)
        sim.check("before: described by the trigger", 1 if kfs[1]["note"] == "C1 pins 1–2 (V) ↓ 3.6788 V" else 0, 1, absolute=0,
                  note=kfs[1]["note"])
    hits2 = hits(c, 2)
    sim.check("before: the trigger's hit kept, not fired again on the replay", len(hits2), 1, absolute=0, note=str(hits2))
    # the state of the key frame before = the state the run passed through at that time
    c("sim", "keyframe", "open", str(kfs[1]["n"]))
    h_kf = c("sim", "hash", "--elements")
    c("sim", "stop")
    c("trigger", "disarm", "1", "2")
    c("sim", "run", "--for", "%dps" % (int(kfs[1]["time_ps"]) - 1), timeout=300)   # A run starts at 1 ps
    h_run = c("sim", "hash", "--elements")
    sim.check("before: the same state as the run at that time", 1 if (h_kf["hash"], h_kf["state"]) == (h_run["hash"], h_run["state"]) else 0,
              1, absolute=0, note="%s %s vs %s %s" % (h_kf["time_ps"], h_kf["hash"], h_run["time_ps"], h_run["hash"]))
    c("sim", "stop")

    # key frames and trigger edits: a trigger added after a key frame was saved; it still loads,
    # without "the circuit changed", and the new trigger starts from its signal
    c("trigger", "add", "C1", "rise:9", "--pause")   # 3
    n = len(c("messages")["lines"])
    c("sim", "keyframe", "open", str(kfs[1]["n"]))    # 10 ms: charged, the falling edge next
    new = c("messages")["lines"][n:]
    sim.check("key frame after a trigger edit: no 'circuit changed'", 0 if any("changed" in l for l in new) else 1, 1, absolute=0,
              note=str([l for l in new if "changed" in l]))
    r = c("sim", "run", "--until-trigger", "--timeout", "30ms", timeout=300)
    sim.check("the added trigger fires on from the key frame", secs(r["time_ps"]), 20e-3 + 2.3026 * TAU, absolute=STEP)
    c("sim", "stop")


def states(sim):
    """Step 2: part states, on led_driver board_mains (frozen copy: its MT7833A reports three blocks)."""
    c = sim.cli
    here = os.path.dirname(os.path.abspath(__file__))
    c("circuit", "open", os.path.join(here, "floating_pair", "board_mains.sim1"), "--discard")
    c("trigger", "add", "MT7833A-1:supply", "any->ON", "--pause")        # 1
    c("trigger", "add", "MT7833A-1:supply", "change", "--mode", "normal")  # 2: the start state (UVLO) does not fire
    c("trigger", "add", "MT7833A-1:pwm", ">=WAIT", "--mode", "normal")     # 3
    r = c("sim", "run", "--until-trigger", "--timeout", "1s", timeout=600)
    log = [e for e in c("status", "MT7833A-1")["history"] if e["block"] == "supply" and e["text"] == "ON"]
    sim.check("state: paused when the state log says supply ON", int(r["time_ps"]), int(Cli_ps(log[0]["time"])) if log else 0,
              absolute=1e6, note="%s vs %s (the log's time is text: 1 us)" % (r["time"], log[0]["time"] if log else None))
    h2 = hits(c, 2)
    sim.check("state change: the start state did not fire", len(h2), 1, absolute=0, note=str(h2))
    sim.check("state >= WAIT: pwm WAIT COMP at the same step", len(hits(c, 3)), 1, absolute=0)
    try:
        c("trigger", "add", "MT7833A-1:foo", "change")
        ok = 0
    except Exception as e:
        ok = 1 if "supply, fb, pwm" in str(e) else 0
    sim.check("a wrong block names the blocks", ok, 1, absolute=0)
    c("sim", "stop")


def Cli_ps(t):
    """'312.888 ms' -> ps"""
    v, u = t.split()
    return round(float(v) * {"s": 1e12, "ms": 1e9, "us": 1e6, "µs": 1e6, "ns": 1e3, "ps": 1}[u])


if __name__ == "__main__":
    run_test(test)
