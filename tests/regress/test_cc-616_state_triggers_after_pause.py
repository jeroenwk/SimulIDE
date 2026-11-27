"""State triggers keep counting after a pause fired by a voltage trigger (create-circuit #616).

With a single pause trigger on a voltage, the count-only state triggers of a transistor missed
the transitions that followed the pause (the SAT 2.74 ms after the first trip, the next OFF);
with the pause trigger disarmed, or watching the state itself, they counted them all.

Circuit: a 0..5 V square of 50 Hz through 10k into an NPN's base, 1k collector load on a 5 V
rail. Q goes SAT on each rising edge and OFF on each falling one. Triggers Q:state any->SAT and
any->OFF, normal, holdoff 5 ms. Expected over 95 ms: the same hit times with and without a
pause trigger on the collector falling through 2.5 V (paused there, then run on to 95 ms),
and 5 SAT / 5 OFF hits (an edge each 10 ms; the start state does not count).

Not reproduced on the issue's trip circuit and flow after the fixes of #614 (sim average
hanging at a pause) and #615 (a key frame before the trigger undoing the trip, which removed its
hits): kept as a guard.
"""
from harness import run_test


def run(sim, pause):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,0", "Wave_Type=Square", "Freq=50Hz",
      "Always_On=true", "Semi_Ampli=2.5V", "Mid_Volt=2.5V")
    c("comp", "add", "Resistor", "--label", "RB", "--at", "-100,0", "Resistance=10k")
    c("comp", "add", "BJT", "--label", "Q", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "RC", "--at", "0,-100", "--rotate", "90", "Resistance=1k")
    c("comp", "add", "Rail", "--label", "V", "--at", "0,-200", "Voltage=5")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,100")
    c("wire", "add", "W.outnod", "RB.lPin")
    c("wire", "add", "RB.rPin", "Q.base")
    c("wire", "add", "Q.emiter", "G.Gnd")
    c("wire", "add", "Q.collector", "RC.rPin")
    c("wire", "add", "RC.lPin", "V.outnod")
    sat = c("trigger", "add", "Q:state", "any->SAT", "--holdoff", "5ms")["id"]
    off = c("trigger", "add", "Q:state", "any->OFF", "--holdoff", "5ms")["id"]
    if pause:
        c("trigger", "add", "Q.collector", "fall:2.5", "--pause")
        r = c("sim", "run", "--until-trigger", "--timeout", "100ms")
        sim.check("pause trigger fired", 1 if r.get("fired") else 0, 1, absolute=0)
        left = 95e-3 - int(r["time_ps"]) * 1e-12
        c("sim", "run", "--for", "%dps" % round(left * 1e12))
    else:
        c("sim", "run", "--for", "95ms")
    h = [c("trigger", "hits", str(i))[0]["times_ps"] for i in (sat, off)]
    c("sim", "stop")
    return [[int(t) * 1e-12 for t in x] for x in h]


def test(sim):
    ref = run(sim, False)
    got = run(sim, True)
    for k, name in enumerate(("SAT", "OFF")):
        sim.check("no pause: %s hits" % name, len(ref[k]), 5, absolute=0, note=str(ref[k]))
        sim.check("with a pause: %s hits" % name, len(got[k]), len(ref[k]), absolute=0, note=str(got[k]))
        if len(got[k]) == len(ref[k]):
            sim.check("with a pause: %s hit times (s, worst)" % name,
                      max([abs(a - b) for a, b in zip(got[k], ref[k])] or [0]), 0, absolute=2e-6)


run_test(test)
