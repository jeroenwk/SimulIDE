"""Saved key frames (specs/sim-control.md §5.1): a state kept next to the circuit loads again,
in the same run and in another process, and the run goes on bit for bit as it did.

For each circuit (a copy in a temporary folder, so <circuit>.keyframes/ is written there):
- run to t1, `sim keyframe save`, run on by a span, `sim hash` = H;
- in the same process: `sim keyframe open 1` lands on t1, the same span again gives H;
- after a restart (a new process, other addresses: node callbacks and the 6502's routine of the
  current cycle are saved as indices, not pointers): open the circuit, open key frame 1 (the
  simulation starts, paused there), the same span: H again.
Part states (the badges): led_driver board_mains (frozen copy, its MT7833A's three script cores):
key frames at 8 ms (supply UVLO, pwm IDLE) and 378 ms (supply ON, pwm SOFT START); opening the later
one from the earlier (forward: this run's history has nothing there yet), the earlier from the
later, and both in a new process show the state of the key frame (the state texts are in it).
Then: the description is kept in the file (note, reopened), a changed part value loads with a
warning in Messages, another circuit's key frame is refused, delete removes the file, and two
key frames play as a range (from the earlier, paused at the later's time).
The hash is every node voltage, bit for bit (and --elements: every element's saved state), so
"the same" means the same computation, not a close value.
"""
import glob
import os
import shutil
import tempfile

from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))
EX = os.path.join(HERE, "..", "..", "resources", "examples")
TC = os.path.join(HERE, "..", "circuits")

CIRCUITS = [
    ("RC + 555 astable", EX + "/Logic/lm555-astable.sim1", "20ms", "5ms"),
    ("Arduino + DHT22 (node callbacks switched on and off)", EX + "/Micro/Sensors/Arduino_DHT22/dht22_test.sim1", "30ms", "10ms"),
    ("6502 (routine of the current cycle)", EX + "/Micro/mcs-65/6502_ben/6502_ben.sim1", "5ms", "2ms"),
    ("ternary AND debouncer (static: no event pending)", TC + "/history_ternary_and_debouncer.sim1", "3ms", "2ms"),
    ("PIC 16F887 CCP + logic analyzer", EX + "/Micro/Pic/16F88x/16f887_CCP/887_ccp.sim1", "5ms", "2ms"),
]


def copy_circuit(path, tmp):
    """The circuit's folder into tmp (firmware and subcircuits next to it come along)."""
    src = os.path.dirname(os.path.abspath(path))
    dst = os.path.join(tmp, os.path.basename(src))
    if not os.path.exists(dst):
        shutil.copytree(src, dst)
    return os.path.join(dst, os.path.basename(path))


def state(c):
    h = c("sim", "hash", "--elements")
    return h["hash"], h["state"], int(h["time_ps"])


def run(c, span):
    c("sim", "run", "--for", span, timeout=600)


def test_all(sim):
    tmp = tempfile.mkdtemp(prefix="simulide-keyframes-")
    c = sim.cli
    c("settings", "set", "history", "true")
    results = {}
    for label, path, t1, span in CIRCUITS:
        circ = copy_circuit(path, tmp)
        c("circuit", "open", circ, "--discard")
        c("sim", "speed", "1000000")
        run(c, t1)
        kf = c("sim", "keyframe", "save", "--note", "at t1")
        sim.check(label + ": saved as key frame 1", kf["n"], 1, absolute=0)
        t_saved = int(kf["time_ps"])
        run(c, span)
        h = state(c)
        r = c("sim", "keyframe", "open", "1")
        sim.check(label + ": open lands on t1", int(r["time_ps"]), t_saved, absolute=0)
        run(c, span)
        h2 = state(c)
        sim.check(label + ": same run, node voltages", 1 if h2[0] == h[0] else 0, 1, absolute=0, note="%s vs %s" % (h2[0], h[0]))
        sim.check(label + ": same run, full state", 1 if h2[1] == h[1] else 0, 1, absolute=0)
        results[label] = (circ, t_saved, span, h)
        c("sim", "stop")

    # Part states after a jump: board_mains
    bm = copy_circuit(os.path.join(HERE, "floating_pair", "board_mains.sim1"), tmp)
    c("circuit", "open", bm, "--discard")
    c("sim", "speed", "1000000")

    def ic_state():
        return " ".join("%s:%s" % (x["block"], x["text"]) for x in c("status", "IC1")["status"])
    c("sim", "run", "--for", "8ms", timeout=900)
    early = ic_state()
    c("sim", "keyframe", "save", "--note", "early")
    c("sim", "run", "--for", "370ms", timeout=900)
    late = ic_state()
    c("sim", "keyframe", "save", "--note", "late")
    sim.check("board_mains: the IC state changed between the key frames", 1 if early != late else 0, 1, absolute=0, note=early + " / " + late)
    for n, want, what in [(1, early, "back"), (2, late, "forward")]:
        c("sim", "keyframe", "open", str(n))
        got = ic_state()
        sim.check("board_mains: IC state after going %s to key frame %d" % (what, n), 1 if got == want else 0, 1, absolute=0, note=got)
    results["board_mains part states"] = (bm, early, late)
    c("sim", "stop")

    HEADLESS.restart()                                       # Same socket: sim.cli talks to the new process
    c("settings", "set", "history", "true")
    bm, early, late = results.pop("board_mains part states")
    c("circuit", "open", bm, "--discard")
    for n, want in [(2, late), (1, early)]:
        c("sim", "keyframe", "open", str(n))
        got = " ".join("%s:%s" % (x["block"], x["text"]) for x in c("status", "IC1")["status"])
        sim.check("board_mains: new process, IC state at key frame %d" % n, 1 if got == want else 0, 1, absolute=0, note=got)
    c("sim", "stop")
    for label, (circ, t_saved, span, h) in results.items():
        c("circuit", "open", circ, "--discard")
        c("sim", "speed", "1000000")
        lst = c("sim", "keyframe", "list")["keyframes"]
        sim.check(label + ": new process lists the key frame", len(lst), 1, absolute=0)
        sim.check(label + ": its description", 1 if lst and lst[0]["note"] == "at t1" else 0, 1, absolute=0)
        r = c("sim", "keyframe", "open", "1")
        sim.check(label + ": new process, open lands on t1", int(r["time_ps"]), t_saved, absolute=0)
        run(c, span)
        h3 = state(c)
        sim.check(label + ": new process, node voltages", 1 if h3[0] == h[0] else 0, 1, absolute=0, note="%s vs %s" % (h3[0], h[0]))
        sim.check(label + ": new process, full state", 1 if h3[1] == h[1] else 0, 1, absolute=0)
        sim.check(label + ": new process, time", h3[2], h[2], absolute=0)
        c("sim", "stop")

    # Description, changed circuit, another circuit, range play, delete: on the 555
    circ = results["RC + 555 astable"][0]
    c("circuit", "open", circ, "--discard")
    c("sim", "speed", "1000000")
    c("sim", "keyframe", "note", "1", "first", "edge")
    c("sim", "keyframe", "open", "1")
    run(c, "4ms")
    second = c("sim", "keyframe", "save", "--note", "second")
    sim.check("second key frame numbered 2", second["n"], 2, absolute=0)
    c("circuit", "open", circ, "--discard")                  # Read back from the files
    lst = c("sim", "keyframe", "list")["keyframes"]
    sim.check("two key frames, by time", [k["n"] for k in lst] == [1, 2] and 1 or 0, 1, absolute=0, note=str(lst))
    sim.check("description rewritten in the file", 1 if lst[0]["note"] == "first edge" else 0, 1, absolute=0, note=lst[0]["note"])

    r = c("sim", "keyframe", "play", "1", "2", timeout=600)
    sim.check("range play pauses at the later key frame", int(r["time_ps"]), int(second["time_ps"]), absolute=0)
    sim.check("range play ends paused", 1 if r.get("state") == "paused" else 0, 1, absolute=0, note=str(r.get("state")))

    res = c("comp", "list", "--type", "Resistor")
    sim.check("the 555 circuit has a resistor to change", 1 if res else 0, 1, absolute=0)
    if res:
        c("sim", "stop")
        c("comp", "set", res[0]["uid"] if "uid" in res[0] else res[0]["label"], "Resistance=4321")
        c("sim", "keyframe", "open", "1")
        text = "\n".join(c("messages", "--grep", "Key frame")["lines"]) if isinstance(c("messages"), dict) else str(c("messages"))
        sim.check("changed part value: loads with a warning", 1 if "saved before the circuit changed" in text else 0, 1, absolute=0, note=text[-300:])
        c("sim", "stop")

    other = copy_circuit(EX + "/Switches/keypad.sim1", tmp)
    kdir = os.path.splitext(other)[0] + ".keyframes"
    os.makedirs(kdir, exist_ok=True)
    shutil.copy(lst[0]["file"], kdir)
    c("circuit", "open", other, "--discard")
    try:
        c("sim", "keyframe", "open", "1")
        refused = 0
    except Exception as e:
        refused = 1 if "another circuit" in str(e) else 0
    sim.check("another circuit's key frame is refused", refused, 1, absolute=0)
    c("sim", "stop")

    c("circuit", "open", circ, "--discard")
    c("sim", "keyframe", "delete", "1", "2")
    folder = os.path.splitext(circ)[0] + ".keyframes"
    sim.check("delete removes the files (and the empty folder)", 0 if glob.glob(folder + "/*.kf1") or os.path.isdir(folder) else 1, 1, absolute=0)
    shutil.rmtree(tmp, ignore_errors=True)


if __name__ == "__main__":
    import harness
    orig = harness.Headless.__enter__

    def enter(self):                                         # The test restarts the server itself
        global HEADLESS
        HEADLESS = self
        return orig(self)
    harness.Headless.__enter__ = enter
    run_test(test_all)
