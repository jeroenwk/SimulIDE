"""Meters that read the mean or the r.m.s. value (create-circuit #109).

A Voltimeter or Amperimeter shows the instantaneous value, which swings with an a.c.
signal, while books (and a real true-RMS meter) give the r.m.s. value. Property
`Reading`: Instant (default, as before), Mean or RMS, over blocks of `Avg_Time`
of simulated time (default 100 ms; the display updates once per block, like a real
meter a few times a second). Every settled solver step counts (time-weighted).

A 1 kHz sine of 1 V amplitude (WaveGen, Mid_Volt 0) across a voltmeter and, through
an ammeter, a 1 kΩ resistor:
- RMS: 0.7071 V and 0.7071 mA; Mean: 0 V; Instant: the default, unchanged.
- `read` gives the same value as the display, and says the reading.
- Reading and Avg_Time survive save and reopen.
"""

import os
import tempfile
from harness import run_test

RMS = 2 ** -0.5


def build(sim, reading=None):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "WG", "--at", "-80,0", "Wave_Type=Sine", "Freq=1kHz",
      "Semi_Ampli=1", "Mid_Volt=0", "Always_On=true", "Steps=200")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    extra = ["Reading=%s" % reading] if reading else []
    c("comp", "add", "Voltimeter", "--label", "VM", "--at", "40,-80", *extra)
    c("comp", "add", "Amperimeter", "--label", "AM", "--at", "40,40", *extra)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "120,40", "Resistance=1000")
    c("wire", "add", "WG.outnod", "VM.lPin")
    c("wire", "add", "VM.rPin", "G.Gnd")
    c("wire", "add", "WG.outnod", "AM.lPin")
    c("wire", "add", "AM.rPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "G.Gnd")


def readings(sim, t="350ms"):
    sim.cli("sim", "run", "--for", t)
    r = sim.cli("read", "VM", "AM")
    sim.cli("sim", "stop")
    return r[0], r[1]


def test(sim):
    build(sim)
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "VM")["properties"]}
    sim.check("Reading defaults to Instant", 1 if props.get("Reading") == "Instant" else 0, 1, absolute=0)

    build(sim, "RMS")
    v, a = readings(sim)
    sim.check("voltmeter RMS (V)", v["value"], RMS, tol=0.005)
    sim.check("ammeter RMS (A)", abs(a["value"]), RMS / 1000, tol=0.005)
    sim.check("read says the reading", 1 if v.get("reading") == "RMS" else 0, 1, absolute=0)

    build(sim, "Mean")
    v, a = readings(sim)
    sim.check("voltmeter mean (V)", v["value"], 0.0, absolute=0.005)

    sim.cli("comp", "set", "VM", "Reading=RMS", "Avg_Time=20ms")
    p = os.path.join(tempfile.mkdtemp(prefix="meter-reading-"), "m.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "VM")["properties"]}
    sim.check("reopened Reading=RMS, Avg_Time 20 ms",
              1 if props.get("Reading") == "RMS" and props.get("Avg_Time") == "20 ms" else 0, 1, absolute=0)
    v, _ = readings(sim, "100ms")
    sim.check("reopened RMS (V)", v["value"], RMS, tol=0.005)


if __name__ == "__main__":
    run_test(test)
