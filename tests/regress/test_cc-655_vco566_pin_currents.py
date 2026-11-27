"""A Vco566's pins report the currents its internal elements draw (create-circuit #655).

The 566 loads its VC pin with 1 MOhm (the control terminal's input resistance [DOC]) and drives
R1 and the square / triangle outputs through internal elements on the pins' nodes, but
`comp get` gave 0 A at those pins: only the pin's own stamps counted.
Expected (V+ 12 V, VC 10 V from a rail, R1 10k from V+, C1 10 nF, square into 10k to ground):
- VC pin: 10 V / 1 MOhm = 10 uA into the part;
- R1 pin: (12 - 10) V / 10k = 200 uA into the part (pin 6 held at VC);
- the square output's pin delivers what its 10k load takes (current out of the part = V/10k).
"""
from harness import run_test


def pin_current(c, comp, pid):
    return next(p["current"] for p in c("comp", "get", comp)["pins"] if p["id"] == pid)


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "VP", "--at", "200,-120", "Voltage=12")
    c("comp", "add", "Vco566", "--label", "U", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-120,0", "Resistance=10k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-120,60", "Capacitance=10n")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "120,40", "Resistance=10k")
    c("comp", "add", "Ground", "--label", "G", "--at", "100,120")
    c("comp", "add", "Rail", "--label", "VC", "--at", "-200,-80", "Voltage=10")
    for a, b in (("VP.outnod", "U.vp"), ("U.gnd", "G.Gnd"), ("VP.outnod", "R1.lPin"), ("R1.rPin", "U.r1"),
                 ("U.c1", "C1.lPin"), ("C1.rPin", "G.Gnd"), ("VC.outnod", "U.vc"), ("U.sq", "RL.lPin"),
                 ("RL.rPin", "G.Gnd")):
        c("wire", "add", a, b)
    c("sim", "run", "--for", "1ms")
    sim.check("VC pin current (A)", pin_current(c, "U", "vc"), 10e-6, tol=0.01)
    sim.check("R1 pin current (A)", pin_current(c, "U", "r1"), 200e-6, tol=0.01)
    v = c("probe", "U.sq")[0]["voltage"]
    sim.check("square pin current = -V/10k (A)", pin_current(c, "U", "sq"), -v / 10e3, tol=0.01)
    c("sim", "stop")


run_test(test)
