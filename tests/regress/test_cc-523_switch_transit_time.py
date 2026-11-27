"""A switch's armature transit: the pole touches nothing for Transit_Time (create-circuit #523).

A changeover's bounce burst started at the click and its first make came within one step:
a real form-C switch leaves the old throw, travels for some ms (AoE fig 12.15 note), then
makes the new one. So an SPDT keeper/latch could not be exercised over the transit gap.

Circuit: pole on 5 V, each throw to ground through 10 k. Transit_Time 2 ms, no bounce,
toggled at 1 ms, sampled every 100 us:
- DT: the throw it leaves drops at once (0 V at the first sample), the throw it makes
  stays at 0 V until 2 ms later and is 5 V from then on; between, both throws are 0 V;
- SPST closing: open for 2 ms, then closed; SPST opening: open at once (a break has no
  travel to wait for);
- Transit_Time 2 ms with Bounce_Time 1 ms: still 0 V at 1.9 ms, 5 V at the end.
"""

from harness import run_test


def run(sim, dt, props=(), close=True):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-80,0", "Voltage=5", "Out=true")
    c("comp", "add", "Switch", "--label", "S", "--at", "0,0", "DT=%s" % ("true" if dt else "false"),
      "Transit_Time=2 ms", *props)
    c("comp", "add", "Resistor", "--label", "R0", "--at", "80,0", "Resistance=10000")
    c("comp", "add", "Ground", "--label", "G", "--at", "200,120")
    c("wire", "add", "V.outnod", "S.pinP0")
    c("wire", "add", "S.switch0pinN", "R0.lPin")
    c("wire", "add", "R0.rPin", "G.Gnd")
    if dt:
        c("comp", "add", "Resistor", "--label", "R1", "--at", "80,80", "Resistance=10000")
        c("wire", "add", "S.switch1pinN", "R1.lPin")
        c("wire", "add", "R1.rPin", "G.Gnd")
    if not close:
        c("comp", "set", "S", "Toggled=true")
    c("sim", "start")
    c("sim", "run", "--for", "1ms")
    c("comp", "set", "S", "Toggled=%s" % ("true" if close else "false"))
    v0, v1 = [], []
    for _ in range(30):
        c("sim", "run", "--for", "100us")
        v0.append(c("probe", "S.switch0pinN")[0]["voltage"])
        if dt:
            v1.append(c("probe", "S.switch1pinN")[0]["voltage"])
    c("sim", "stop")
    return v0, v1


def test(sim):
    v0, v1 = run(sim, True)
    sim.check("DT: old throw released at once (V at +0.1 ms)", v1[0], 0, absolute=0.05)
    sim.check("DT: old throw stays released (max V)", max(v1), 0, absolute=0.05)
    sim.check("DT: new throw open in transit (max V to +1.9 ms)", max(v0[:19]), 0, absolute=0.05)
    sim.check("DT: new throw made after transit (min V from +2.1 ms)", min(v0[21:]), 5, absolute=0.05)
    v0, _ = run(sim, False)
    sim.check("SPST closing: open in transit (max V to +1.9 ms)", max(v0[:19]), 0, absolute=0.05)
    sim.check("SPST closing: closed after transit (V at the end)", v0[-1], 5, absolute=0.05)
    v0, _ = run(sim, False, close=False)
    sim.check("SPST opening: open at once (V at +0.1 ms)", v0[0], 0, absolute=0.05)
    v0, _ = run(sim, False, ("Bounce_Time=1 ms",))
    sim.check("transit + bounce: open at +1.9 ms", v0[18], 0, absolute=0.05)
    sim.check("transit + bounce: closed at the end", v0[-1], 5, absolute=0.05)


run_test(test)
