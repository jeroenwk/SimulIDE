"""A WaveGen's level or shape changed while the run is paused applies when the run goes on
(create-circuit #773, #505, #501).

A Square only re-evaluated its output at its next edge, so a new Mid_Volt, Semi_Ampli or
Wave_Type waited up to half a period: at 10 Hz the continued run kept the old 1 V for 50 ms,
and a build stepping a light level through a slow source read the old one.
Expected by hand, a WaveGen into 1 k to ground:
- Square 10 Hz, Mid 0.5, Semi 0.5: high at 10 ms = 1 V.
- Mid_Volt 2 set while paused, 5 ms more: 2.5 V (still in the high half).
- Sine, Semi 1 uV, Mid 3 while paused, 5 ms more: 3 V.
- The phase is kept: a Square 10 Hz changed at 10 ms still falls at 50 ms (low at 55 ms).
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def setup(c, *props):
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "0,0", "Always_On=true", *props)
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,0", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,40")
    c("wire", "add", "W.outnod", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")


def test(sim):
    c = sim.cli
    v = lambda: c("probe", "R.lPin")[0]["voltage"]
    setup(c, "Wave_Type=Square", "Freq=10Hz", "Mid_Volt=0.5", "Semi_Ampli=0.5")
    c("sim", "run", "--for", "10ms")
    within(sim, "square high at 10 ms", v(), 0.99, 1.01)
    c("comp", "set", "W", "Mid_Volt=2")
    c("sim", "run", "--for", "5ms")
    within(sim, "Mid_Volt 2 set while paused, 5 ms later", v(), 2.49, 2.51)
    c("comp", "set", "W", "Wave_Type=Sine", "Semi_Ampli=1uV", "Mid_Volt=3")
    c("sim", "run", "--for", "5ms")
    within(sim, "Sine at 3 V set while paused, 5 ms later", v(), 2.99, 3.01)
    c("sim", "stop")

    setup(c, "Wave_Type=Square", "Freq=10Hz", "Mid_Volt=0.5", "Semi_Ampli=0.5")
    c("sim", "run", "--for", "10ms")
    c("comp", "set", "W", "Semi_Ampli=1")
    c("sim", "run", "--for", "35ms")
    within(sim, "high at 45 ms (Semi 1: 1.5 V)", v(), 1.49, 1.51)
    c("sim", "run", "--for", "10ms")
    within(sim, "phase kept: low at 55 ms (-0.5 V)", v(), -0.51, -0.49)
    c("sim", "stop")


run_test(test)
