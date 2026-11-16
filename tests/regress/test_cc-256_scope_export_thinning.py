"""`scope data` thinning keeps peaks, edges and real instants (create-circuit #256, #202, #217, #178).

Over --max samples, `scope data` took every Nth sample of each channel: narrow peaks fell
between the kept samples, edges were dated up to N samples late, and two channels kept
different instants, which read as a phase shift between them (#202). It also gave no sign
that it had thinned, nor the scope's sample slot (time/div / 5000), which sets the
resolution (#178).

Circuit: a 5 kHz clock into a 1 nF / 2 kOhm differentiator (tau 2 us): +-5 V spikes a few
us wide on a trace that changes every step (tens of thousands of samples in 60 ms);
channel 1 is the clock itself (a logic level: a sample per edge).
Right result, --max 2000:
- the reply says it thinned, and gives the sample slot (1 ms/div: 0.2 us);
- every returned sample is a real stored sample (time and value as in the full export);
- the highest and lowest value equal the full export's (peaks kept);
- the clock channel, thinned on the same buckets, keeps both its levels;
- `scope get` gives sampleSlot and historyMin.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Clock", "--label", "CK", "--at", "-300,0", "Freq=5000", "Always_On=true")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-150,0", "Capacitance=1n")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,80", "--rotate", "90", "Resistance=2k")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,200")
    c("comp", "add", "Oscope", "--label", "SC", "--at", "300,0", "InputImped=10G")
    c("wire", "add", "CK.outnod", "C1.lPin")
    c("wire", "add", "C1.rPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "G.Gnd")
    c("wire", "add", "R1.lPin", "SC.Pin0")
    c("wire", "add", "CK.outnod", "SC.Pin1")
    c("scope", "set", "SC", "--timediv", "1ms")
    sim.run("60ms")

    full = c("scope", "data", "SC", "--max", "0")
    thin = c("scope", "data", "SC", "--max", "2000")
    f0, t0 = full["channels"][0]["samples"], thin["channels"][0]["samples"]
    f1, t1 = full["channels"][1]["samples"], thin["channels"][1]["samples"]
    sim.check("full export has many samples (%d)" % len(f0), int(len(f0) > 4000), 1, absolute=0)
    sim.check("thinned to at most --max (%d)" % len(t0), int(len(t0) <= 2000), 1, absolute=0)
    sim.check("reply says it thinned", int("thinned" in thin), 1, absolute=0)
    sim.check("sample slot at 1 ms/div (s)", thin.get("sampleSlot", 0), 2e-7, tol=1e-6)
    real = {(round(t, 12), v) for t, v in f0}
    sim.check("every thinned sample is a stored one",
              int(all((round(t, 12), v) in real for t, v in t0)), 1, absolute=0)
    sim.check("highest value kept (V)", max(v for _, v in t0), max(v for _, v in f0), tol=0, absolute=1e-12)
    sim.check("lowest value kept (V)", min(v for _, v in t0), min(v for _, v in f0), tol=0, absolute=1e-12)
    sim.check("clock channel keeps both levels (%d of %d samples)" % (len(t1), len(f1)),
              int(max(v for _, v in t1) == max(v for _, v in f1) and min(v for _, v in t1) == min(v for _, v in f1)), 1, absolute=0)
    g = c("scope", "get", "SC")
    sim.check("scope get gives the slot and history (%s, %s)" % (g.get("sampleSlot"), g.get("historyMin")),
              int("sampleSlot" in g and "historyMin" in g), 1, absolute=0)


run_test(test)
