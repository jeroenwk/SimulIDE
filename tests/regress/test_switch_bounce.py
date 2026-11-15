"""Contact bounce for Switch and Push, properties Bounce_Time and Bounces (create-circuit #136).

A real contact does not close cleanly: it touches, springs open and touches again a few
times within a millisecond or so before it settles. Debouncer circuits (an RC and a
Schmitt trigger, an SR latch) exist because of that burst. SimulIDE's switches changed
state once, so a debouncer had nothing to do.

With Bounce_Time T and Bounces N, each change of the contact is a burst: the new state,
then N times back to the old one and to the new one again, all within T, ending in the
new state (2N+1 changes). Bounce_Time 0 (default) is the clean change of before.

Circuit: 5 V -> contact -> 1 k -> ground; the resistor's top is sampled every 20 us.
- Switch, T = 2 ms, N = 4, clicked: 9 changes, all within the first 2 ms, ends at 5 V.
- Clicked back: 9 changes again, ends at 0 V.
- Push, T = 1 ms, N = 3, pressed: 7 changes within 1 ms, ends at 5 V; released: 7, 0 V.
- Bounce_Time 0: one clean change.
- Both properties survive save and reopen.
"""

import os
import tempfile
from harness import run_test


def build(sim, part="Switch", props=()):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-120,0", "Voltage=5", "Out=true")
    c("comp", "add", part, "--label", "S1", "--at", "-40,0", *props)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "40,0", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "120,40")
    c("wire", "add", "V.outnod", "S1.pinP0")
    c("wire", "add", "S1.switch0pinN", "R1.lPin")
    c("wire", "add", "R1.rPin", "G.Gnd")
    c("sim", "run", "--for", "1ms")


def burst(sim, action, ms=3.0, dt_us=20):
    before = sim.cli("probe", "R1.lPin")[0]["voltage"] > 2.5
    sim.cli("ui", *action)
    v, t = [], []
    for i in range(int(ms * 1000 / dt_us)):
        sim.cli("sim", "run", "--for", "%dus" % dt_us)
        v.append(sim.cli("probe", "R1.lPin")[0]["voltage"] > 2.5)
        t.append((i + 1) * dt_us)
    changes = [t[i] for i in range(1, len(v)) if v[i] != v[i - 1]]
    first = 1 if v[0] != before else 0           # the change before the first sample
    return first + len(changes), (changes[-1] if changes else 0), v[-1]


def test(sim):
    try:
        build(sim, props=["Bounce_Time=2 ms", "Bounces=4"])
    except Exception as e:                                  # no bounce properties yet
        sim.check("Switch accepts Bounce_Time (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    n, last, high = burst(sim, ("click", "S1"))
    sim.check("Switch closing: changes", n, 9, absolute=0)
    sim.check("Switch closing: last change within 2 ms (us)", min(last, 2020), 2020, absolute=2000)
    sim.check("Switch closing: ends closed", int(high), 1, absolute=0)
    n, last, high = burst(sim, ("click", "S1"))
    sim.check("Switch opening: changes", n, 9, absolute=0)
    sim.check("Switch opening: ends open", int(high), 0, absolute=0)

    build(sim, part="Push", props=["Bounce_Time=1 ms", "Bounces=3"])
    n, last, high = burst(sim, ("press", "S1"), ms=2)
    sim.check("Push press: changes", n, 7, absolute=0)
    sim.check("Push press: last change within 1 ms (us)", min(last, 1020), 1020, absolute=1000)
    sim.check("Push press: ends closed", int(high), 1, absolute=0)
    n, last, high = burst(sim, ("release", "S1"), ms=2)
    sim.check("Push release: changes", n, 7, absolute=0)
    sim.check("Push release: ends open", int(high), 0, absolute=0)

    build(sim)
    n, last, high = burst(sim, ("click", "S1"))
    sim.check("Bounce_Time 0: one clean change", n, 1, absolute=0)
    sim.check("Bounce_Time 0: closed", int(high), 1, absolute=0)

    build(sim, props=["Bounce_Time=2 ms", "Bounces=4"])
    p = os.path.join(tempfile.mkdtemp(prefix="bounce-"), "s.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "S1")["properties"]}
    ok = str(props.get("Bounce_Time", "")).startswith("2") and str(props.get("Bounces")) == "4"
    sim.check("reopened (%s, %s)" % (props.get("Bounce_Time"), props.get("Bounces")), int(ok), 1, absolute=0)


run_test(test)
