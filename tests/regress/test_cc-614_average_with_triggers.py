"""sim average and triggers: a pause trigger ends the average, and its Averager does not turn key
frames off (create-circuit #613, #614).

The Averager was an element of the circuit: while it was in, key frames were off ("Key frames
are off for this circuit: Averager x1 (no state code yet)") and a key-frame trigger only paused.
A pause trigger inside the span left the average waiting for its end forever: the command never
replied, and the Averager stayed in (x2 after the next run).

RC (1 k, 1 uF, tau 1 ms) on a 0..10 V square of 50 Hz, circuit saved to a file. Expected:
- `sim average --for 5ms C1` with a pause trigger at C1 rise 6.3212 V (1 ms): replies, paused at
  1 ms, the span cut there (`to` 1 ms, `paused_early` true), the mean of C1's voltage over
  0..1 ms = 10 (1 - (1 - e^-1)) = 3.679 V (the integral of 10 (1 - e^-t/tau) over one tau);
- then `sim keyframe save` is not refused;
- a key-frame trigger (C1 fall 3.6788 V, at 11 ms) during a `sim average --for 15ms C1` saves a
  key frame (one more in `sim keyframe list`) and the average goes to its end (16 ms).
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


def test(sim):
    c = sim.cli
    d = tempfile.mkdtemp(prefix="avgtrig")
    build(c, os.path.join(d, "rc.sim1"))
    c("trigger", "add", "C1.lPin", "rise:6.3212", "--pause")
    r = c("sim", "average", "--for", "5ms", "C1", timeout=30)
    st = c("sim", "status")
    t = int(st["time_ps"]) * 1e-12
    sim.check("pause trigger: paused at 1 ms (s)", t, 1e-3, absolute=5e-6)
    sim.check("pause trigger: reply marks the early end", 1 if r.get("paused_early") else 0, 1, absolute=0)
    sim.check("pause trigger: span cut at the pause (ps)", int(r["span_ps"]) * 1e-12, 1e-3, absolute=5e-6)
    mean = r["channels"][0]["voltage"]["mean"]
    sim.check("pause trigger: mean of C1 over 0..1 ms (V)", mean, 10 / math.e, absolute=0.01)

    kf = c("sim", "keyframe", "save")
    sim.check("key frame saved after an average", 1 if "n" in kf or "file" in kf else 0, 1, absolute=0,
              note=str(kf)[:120])

    n0 = len(c("sim", "keyframe", "list")["keyframes"])
    c("trigger", "add", "C1", "fall:3.6788", "--keyframe")
    r = c("sim", "average", "--for", "15ms", "C1", timeout=60)
    st = c("sim", "status")
    sim.check("key-frame trigger: average ran to its end (s)", int(st["time_ps"]) * 1e-12, 16e-3, absolute=5e-6)
    n1 = len(c("sim", "keyframe", "list")["keyframes"])
    sim.check("key-frame trigger: a key frame saved during the average", n1 - n0, 1, absolute=0)
    msgs = c("messages")["lines"]
    sim.check("no 'Key frames are off' in Messages", sum("Key frames are off" in l for l in msgs), 0, absolute=0)
    c("sim", "stop")


run_test(test)
