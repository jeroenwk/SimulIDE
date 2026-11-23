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
Steps 3-4: the Triggers pane (tab / split, the editor opens), a part's Triggers tab, transistor
and diode pins by name (G-S, B-E, A->K), the scope's own trigger (fixed 5 V and Auto: ln2 tau).
Step 5: AND - an event and qualifiers read as states at its step (current crossings only while C1
is below 5 V: the rising edges), the event changed, a ↕ member only as the event, saved and
reopened, split, undo, one member left becomes the row; a state qualifier (supply is ON).
Step 6: deleting a part deletes its own triggers in the same undo step, moves a node trigger to
another pin of the net (it fires there), leaves an AND's other member as the row; copying a part
copies its own triggers (not ANDs); undo of either brings everything back.
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
    pane(sim)
    parts(sim)
    combined(sim)
    edits(sim)


def edits(sim):
    """Step 6: deleting a part (D18, §5) and copying it (§9)."""
    c = sim.cli
    build(c)
    own = c("trigger", "add", "C1", "rise:5")["id"]
    node = c("trigger", "add", "node:C1.lPin", "fall:5", "--pause", "--mode", "normal")["id"]
    keep = c("trigger", "add", "R1:i", "rise:1mA")["id"]
    m1 = c("trigger", "add", "C1", "rise:9")["id"]
    m2 = c("trigger", "add", "W.outnod", "rise:5")["id"]
    anded = c("trigger", "combine", str(m1), str(m2))["id"]
    sim.check("node trigger: named by its anchor", 1 if c("trigger", "list")[1]["watches"] == "node at C1 pin 1" else 0, 1, absolute=0,
              note=c("trigger", "list")[1]["watches"])
    def defs():
        strip = lambda r: {k: v for k, v in r.items() if k not in ("hits", "last")}
        return [dict(strip(r), members=[strip(m) for m in r.get("members", [])]) for r in c("trigger", "list")]
    before = defs()

    c("comp", "remove", "C1")
    rows = {r["id"]: r for r in c("trigger", "list")}
    sim.check("part deleted: its own trigger goes", 0 if own in rows else 1, 1, absolute=0)
    sim.check("part deleted: another part's trigger stays", 1 if keep in rows else 0, 1, absolute=0)
    sim.check("part deleted: the node trigger moves to another pin of the net",
              1 if node in rows and rows[node]["watches"] == "node at R1 pin 2" else 0, 1, absolute=0, note=str(rows.get(node)))
    sim.check("part deleted: an AND left with one member becomes that row",
              1 if anded not in rows and m2 in rows and "members" not in rows[m2] and rows[m2]["mode"] == "single" else 0, 1, absolute=0,
              note=str(rows.get(m2)))
    r = c("sim", "run", "--for", "20ms")
    sim.check("the moved node trigger fires (R1 pin 2 follows the wave now)", secs(r["time_ps"]), 10e-3, absolute=STEP)
    c("sim", "stop")
    c("undo")
    sim.check("undo: the part and all its triggers back", 1 if defs() == before else 0, 1, absolute=0, note=str(defs()))

    new = c("comp", "copy", "C1", "--by", "0,160")
    sim.check("copy: one new part", len(new), 1, absolute=0, note=str(new))
    pasted = [r for r in c("trigger", "list") if new and r["watches"].split(" ")[0] != "C1" and r.get("condition") == "↑ 5 V"
              and "pins 1–2" in r["watches"]]
    sim.check("copy: its own trigger comes along, on the new part", len(pasted), 1, absolute=0, note=str(c("trigger", "list")))
    sim.check("copy: not the AND (combinations are not copied)", len(c("trigger", "list")), len(before)+1, absolute=0)
    c("undo")
    sim.check("undo the paste: its trigger goes too", 1 if defs() == before else 0, 1, absolute=0)


def combined(sim):
    """Step 5: AND (§3). An event and qualifiers read as states at its step."""
    c = sim.cli
    build(c)
    a = c("trigger", "add", "R1:i", "cross:0", "--mode", "normal")["id"]     # Every edge of the wave
    b = c("trigger", "add", "C1", "fall:5")["id"]                            # Qualifier: C1 is below 5 V
    try:
        c("trigger", "combine", str(a))
        ok = 0
    except Exception:
        ok = 1
    sim.check("combine needs two rows", ok, 1, absolute=0)
    x = c("trigger", "combine", str(a), str(b), "--name", "edge while low")
    roles = [(m["id"], m["role"]) for m in x["members"]]
    sim.check("AND: the current crossing is the event, C1 a qualifier", 1 if roles == [(a, "event"), (b, "qualifier")] else 0, 1,
              absolute=0, note=str(roles))
    sim.check("AND: the qualifier reads 'is below 5 V'", 1 if x["members"][1]["reads"] == "is below 5 V" else 0, 1, absolute=0)
    sim.check("AND: the first row's action and mode", 1 if (x["action"], x["mode"]) == ("count only", "normal") else 0, 1, absolute=0)
    c("sim", "run", "--for", "101ms")
    h = hits(c, x["id"])
    sim.check("AND: fires only at the rising edges (C1 low)", len(h), 5, absolute=0, note=str(h))
    sim.check("AND: the first at 20 ms", h[0] if h else 0, 20e-3, absolute=STEP)
    try:
        c("trigger", "set", str(x["id"]), "--event", str(b))
        ok = 0
    except Exception:
        ok = 1
    sim.check("a ↕ member can only be the event", ok, 1, absolute=0)
    c("sim", "stop")

    # changing the event: C1 rising through 5 V (only while the wave is high) with "the wave is above 5 V" fires
    # each period; with the wave's rising edge as the event, C1 is then near 0, never "above 5 V": no hit
    e = c("trigger", "add", "C1", "rise:5", "--mode", "normal")["id"]
    q = c("trigger", "add", "W.outnod", "rise:5")["id"]
    y = c("trigger", "combine", str(e), str(q))["id"]
    c("sim", "run", "--for", "100ms")
    sim.check("AND event C1 ↑5 V, wave high: once per period", len(hits(c, y)), 5, absolute=0)
    c("sim", "stop")
    c("trigger", "set", str(y), "--event", str(q))
    c("sim", "run", "--for", "100ms")
    sim.check("event changed to the wave's edge: C1 is never above 5 V then", len(hits(c, y)), 0, absolute=0)
    c("sim", "stop")

    # saved, reopened, split, undo, a member deleted
    path = os.path.join(tempfile.mkdtemp(prefix="simulide-triggers-"), "and.sim1")
    c("circuit", "save", path)
    with open(path) as f:
        text = f.read()
    sim.check(".sim1: join=and with its members, event marked", 1 if 'join="and"' in text and 'event="1"' in text else 0, 1, absolute=0)
    def defs():          # The definitions (hits are this run's)
        strip = lambda r: {k: v for k, v in r.items() if k not in ("hits", "last")}
        return [dict(strip(r), members=[strip(m) for m in r.get("members", [])]) for r in c("trigger", "list")]
    before = defs()
    c("circuit", "open", path, "--discard")
    sim.check("reopened: the same ANDs", 1 if defs() == before else 0, 1, absolute=0, note=str(defs()))
    rows = c("trigger", "split", str(x["id"]))
    sim.check("split: the members back as rows, disarmed", 1 if [r["id"] for r in rows] == [a, b] and not any(r["armed"] for r in rows) else 0,
              1, absolute=0, note=str(rows))
    c("undo")
    sim.check("undo: the AND again", 1 if defs() == before else 0, 1, absolute=0)
    c("trigger", "del", str(q))
    t = [r for r in c("trigger", "list") if r["id"] == e]
    sim.check("one member left: it is the row again, with the AND's mode", 1 if t and "members" not in t[0] else 0, 1, absolute=0, note=str(t))

    # a part's state as a qualifier (board_mains): the pwm block's change while supply is ON
    here = os.path.dirname(os.path.abspath(__file__))
    c("circuit", "open", os.path.join(here, "floating_pair", "board_mains.sim1"), "--discard")
    ev = c("trigger", "add", "MT7833A-1:pwm", "change", "--mode", "normal")["id"]
    ql = c("trigger", "add", "MT7833A-1:supply", "any->ON")["id"]
    z = c("trigger", "combine", str(ev), str(ql))
    sim.check("state qualifier reads 'is ON'", 1 if z["members"][1].get("reads") == "is ON" else 0, 1, absolute=0, note=str(z["members"]))
    c("sim", "run", "--for", "400ms", timeout=600)
    pwm = [e2 for e2 in c("status", "MT7833A-1")["history"] if e2["block"] == "pwm"]
    on = [e2 for e2 in c("status", "MT7833A-1")["history"] if e2["block"] == "supply" and e2["text"] == "ON"]
    after_on = [e2 for e2 in pwm if on and Cli_ps(e2["time"]) >= Cli_ps(on[0]["time"])]
    sim.check("pwm changes while supply ON: as many hits as the state log has", len(hits(c, z["id"])), len(after_on), absolute=0,
              note="%s vs %s" % (hits(c, z["id"]), [p2["time"] for p2 in after_on]))
    c("sim", "stop")


def parts(sim):
    """Step 4: the Triggers tab of a part's properties; pin names of transistors and diodes; the scope's
    own trigger as a simulation trigger (D21)."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Mosfet", "--label", "Q1", "--at", "0,0")
    c("comp", "add", "BJT", "--label", "Q2", "--at", "80,0")
    c("comp", "add", "Diode", "--label", "D1", "--at", "160,0")
    rows = [c("trigger", "add", "Q1.G-Q1.S", "rise:4")["watches"], c("trigger", "add", "Q1.D:i", "rise:1")["watches"],
            c("trigger", "add", "Q2.B-Q2.E", "rise:0.6")["watches"], c("trigger", "add", "D1:i", "rise:1")["watches"],
            c("trigger", "add", "Q1:state", "any->ACTIVE")["watches"]]
    sim.check("transistor and diode pins by name", 1 if rows == ["Q1 pins G–S (Vgs)", "Q1 pin D (I)", "Q2 pins B–E (Vbe)",
                                                                 "D1 current (A→K)", "Q1 state"] else 0, 1, absolute=0, note=str(rows))
    for part in ("Q1", "D1"):
        try:
            c("view", "props", part, "--tab", "Triggers")
            ok = 1
        except Exception as e:
            ok, _ = 0, sim.log(str(e))
        sim.check("%s properties: a Triggers tab" % part, ok, 1, absolute=0)

    # the scope: channel 1 on C1 of the RC; a fixed 5 V level fires ln2 tau after each rising edge,
    # Auto (the middle of the range, ~5 V) as well
    build(c)
    c("comp", "add", "Oscope", "--label", "S", "--at", "400,0")
    c("wire", "add", "C1.lPin", "S.Pin0")
    c("comp", "set", "S", "Trigger=0", "TrigAuto=false", "TrigLevel=5")
    t = c("trigger", "add", "scope:S", "--mode", "normal")
    sim.check("scope: its row", 1 if (t["watches"], t["condition"]) == ("S Ch1", "↑ 5 V") else 0, 1, absolute=0, note=str(t))
    c("sim", "run", "--for", "50ms")
    h = hits(c, t["id"])
    sim.check("scope fixed level: 3 rising crossings in 50 ms", len(h), 3, absolute=0)
    sim.check("scope fixed level: at ln2 tau", h[0] if h else 0, 0.6931 * TAU, absolute=STEP)
    c("comp", "set", "S", "TrigAuto=true")
    sim.check("scope Auto: the row follows", 1 if c("trigger", "list")[0]["condition"] == "↑ auto" else 0, 1, absolute=0)
    c("sim", "run", "--for", "100ms")
    h = hits(c, t["id"])
    sim.check("scope Auto: on firing each cycle", len(h), 8, absolute=0, note=str(h))
    sim.check("scope Auto: near the middle (ln2 tau)", h[-1] if h else 0, 140e-3 + 0.6931 * TAU, absolute=5 * STEP)
    c("comp", "set", "S", "Trigger=4")
    sim.check("scope trigger off: a warning on its row", 1 if "warning" in c("trigger", "list")[0] else 0, 1, absolute=0)


def pane(sim):
    """Step 3: the Triggers pane, in the tabs or beside them; the editor opens (headless closes it)."""
    c = sim.cli
    sim.check("view triggers split", 1 if c("view", "triggers", "split")["triggers"] == "split" else 0, 1, absolute=0)
    sim.check("view triggers tab", 1 if c("view", "triggers", "tab")["triggers"] == "tab" else 0, 1, absolute=0)
    c("trigger", "add", "MT7833A-1:pwm", "change")
    c("view", "triggers", "--edit", "4")
    c("view", "triggers", "--new")
    sim.check("the editor opened and closed: the triggers unchanged", len(c("trigger", "list")), 4, absolute=0)


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
