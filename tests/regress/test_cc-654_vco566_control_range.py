"""A Vco566 says when its control voltage leaves the datasheet range (create-circuit #654).

The control terminal works from 0.75 V+ to V+ [DOC]; below it the real part leaves its linear
law, but the model kept oscillating on the formula without a word. Its state now says so.
Expected (V+ 12 V, R1 10k, C1 10 nF): VC 10 V -> RUNNING; VC 7.92 V (< 9 V) -> VC LOW;
VC 12 V -> STOPPED (no timing current).
"""
from harness import run_test


def state(sim, vc):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "VP", "--at", "200,-120", "Voltage=12")
    c("comp", "add", "Vco566", "--label", "U", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-120,0", "Resistance=10k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-120,60", "Capacitance=10n")
    c("comp", "add", "Ground", "--label", "G", "--at", "100,120")
    c("comp", "add", "Rail", "--label", "VC", "--at", "-200,-80", "Voltage=%g" % vc)
    for a, b in (("VP.outnod", "U.vp"), ("U.gnd", "G.Gnd"), ("VP.outnod", "R1.lPin"), ("R1.rPin", "U.r1"),
                 ("U.c1", "C1.lPin"), ("C1.rPin", "G.Gnd"), ("VC.outnod", "U.vc")):
        c("wire", "add", a, b)
    sim.run("2ms")
    st = str(sim.read("U").get("state"))
    c("sim", "stop")
    return st


def test(sim):
    for vc, want in ((10, "RUNNING"), (7.92, "VC LOW"), (12, "STOPPED")):
        st = state(sim, vc)
        sim.check("VC %g V: %s (state %s)" % (vc, want, st), int(st.startswith(want)), 1, absolute=0)


run_test(test)
