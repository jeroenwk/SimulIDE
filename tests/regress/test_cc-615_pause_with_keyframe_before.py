"""A pause trigger and a key-frame-before trigger firing on the same step (create-circuit #615).

The pause was applied while the key frame's trip back to Δ before was running: it cancelled
that skip ("skip cancelled", "key frame before the trigger: skip cancelled"), no key frame was
saved, and the run was left paused Δ before the trigger, the event undone. Separately, `sim
average` (cc.py run --avg) added its Averager to the circuit, which ended the history ("the
circuit changed during the run"), so "before Δ" had nothing to go back with.

RC (1 k, 1 uF, tau 1 ms) on a 0..10 V square of 50 Hz, history on, circuit saved. Triggers on
C1 rise 6.3212 V (101 ms, after a first run of 100 ms that fills the history): one --pause, one --keyframe --before 0.5ms. Expected, for both
`sim run --until-trigger` and `sim average --for 5ms C1`:
- paused at the trigger, 101 ms (not 100.5 ms), C1 at 6.3212 V;
- a key frame saved at 100.5 ms (C1 there: 10 (1 - e^-0.5) = 3.935 V, read by opening it);
- no "skip cancelled" and no "circuit changed during the run" in Messages.
"""
import math
import os
import tempfile

from harness import run_test


def build(c, path):
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "0,0", "Wave_Type=Square", "Freq=50Hz",
      "Always_On=true", "Semi_Ampli=5V", "Mid_Volt=5V")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "100,0", "Resistance=1k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "200,40", "--rotate", "90", "Capacitance=1uF")
    c("comp", "add", "Ground", "--label", "G", "--at", "100,100")
    c("wire", "add", "W.outnod", "R1.lPin")
    c("wire", "add", "R1.rPin", "C1.lPin")
    c("wire", "add", "C1.rPin", "G.Gnd")
    c("circuit", "save", path)
    c("sim", "run", "--for", "100ms")          # History to go back into (captured at the frames)
    c("trigger", "add", "C1.lPin", "rise:6.3212", "--pause")
    c("trigger", "add", "C1.lPin", "rise:6.3212", "--keyframe", "--before", "0.5ms")


def test(sim):
    c = sim.cli
    c("settings", "set", "history", "true")
    for how in ("until-trigger", "average"):
        d = tempfile.mkdtemp(prefix="pkf")
        build(c, os.path.join(d, "rc.sim1"))
        m0 = len(c("messages")["lines"])
        if how == "average":
            c("sim", "average", "--for", "5ms", "C1", timeout=60)
        else:
            c("sim", "run", "--until-trigger", "--timeout", "5ms", timeout=60)
        t = int(c("sim", "status")["time_ps"]) * 1e-12
        sim.check(how + ": paused at the trigger (s)", t, 101e-3, absolute=5e-6)
        sim.check(how + ": C1 at the pause (V)", c("probe", "C1.lPin")[0]["voltage"], 6.3212, absolute=0.01)
        kfs = c("sim", "keyframe", "list")["keyframes"]
        sim.check(how + ": one key frame saved", len(kfs), 1, absolute=0)
        if kfs:
            sim.check(how + ": key frame at 100.5 ms (s)", int(kfs[0]["time_ps"]) * 1e-12, 100.5e-3, absolute=5e-6)
            c("sim", "keyframe", "open", str(kfs[0]["n"]))
            sim.check(how + ": C1 in the key frame (V)", c("probe", "C1.lPin")[0]["voltage"],
                      10 * (1 - math.exp(-0.5)), absolute=0.01)
        msgs = c("messages")["lines"][m0:]
        sim.check(how + ": no 'skip cancelled'", sum("skip cancelled" in l for l in msgs), 0, absolute=0)
        sim.check(how + ": history kept", sum("circuit changed during the run" in l for l in msgs), 0, absolute=0)
        c("sim", "stop")


run_test(test)
