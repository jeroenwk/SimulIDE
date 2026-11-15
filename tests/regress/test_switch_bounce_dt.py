"""Bounce of a changeover (DT) switch (create-circuit #190).

With Bounce_Time > 0 (PR #65) every "away" half of a burst connected the pole to the
OTHER throw again, so a DT switch flapped between both throws. A real SPDT leaves the
old throw once and bounces only at the contact it made: away = touching nothing. That is
why an SPDT debouncer (a NAND latch on the two throws) gives one clean edge: the hover
is its hold state. Here the throws chattered the latch with the burst.

Circuit: pole on 5 V, each throw to ground through 10 k. The switch rests on switch1pinN;
Toggled=true moves it to switch0pinN with Bounce_Time 2 ms, sampled every 20 us:
- switch1pinN (the throw it leaves) never sees 5 V again during the burst;
- switch0pinN (the throw it makes) bounces: it is open (0 V) at some samples, 5 V at the end;
- an SPST (DT=false) keeps bouncing open/closed (5 V at the end, 0 V seen in the burst).
"""

from harness import run_test


def burst(sim, dt):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-80,0", "Voltage=5", "Out=true")
    c("comp", "add", "Switch", "--label", "S", "--at", "0,0", "DT=%s" % ("true" if dt else "false"),
      "Bounce_Time=2 ms")
    c("comp", "add", "Resistor", "--label", "R0", "--at", "80,0", "Resistance=10000")
    c("comp", "add", "Ground", "--label", "G", "--at", "200,120")
    c("wire", "add", "V.outnod", "S.pinP0")
    c("wire", "add", "S.switch0pinN", "R0.lPin")
    c("wire", "add", "R0.rPin", "G.Gnd")
    if dt:
        c("comp", "add", "Resistor", "--label", "R1", "--at", "80,80", "Resistance=10000")
        c("wire", "add", "S.switch1pinN", "R1.lPin")
        c("wire", "add", "R1.rPin", "G.Gnd")
    c("sim", "start")
    c("sim", "run", "--for", "1ms")
    c("comp", "set", "S", "Toggled=true")
    v0, v1 = [], []
    for _ in range(150):
        c("sim", "run", "--for", "20us")
        v0.append(c("probe", "S.switch0pinN")[0]["voltage"])
        if dt:
            v1.append(c("probe", "S.switch1pinN")[0]["voltage"])
    c("sim", "stop")
    return v0, v1


def test(sim):
    v0, v1 = burst(sim, True)
    sim.check("DT: the throw it leaves stays released (max V)", max(v1), 0, absolute=0.05)
    sim.check("DT: the throw it makes bounces open (min V in the burst)", min(v0[:60]), 0, absolute=0.05)
    sim.check("DT: made at the end (V)", v0[-1], 5, absolute=0.05)
    v0, _ = burst(sim, False)
    sim.check("SPST: bounces open (min V in the burst)", min(v0[:60]), 0, absolute=0.05)
    sim.check("SPST: closed at the end (V)", v0[-1], 5, absolute=0.05)


run_test(test)
