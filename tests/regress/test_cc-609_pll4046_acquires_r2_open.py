"""A 4046 PLL with R2 open acquires from power-up (create-circuit #609, guard).

A build reported a deadlock: with R2 open the VCO has no minimum frequency, at VCO IN = 0 it
stands still, and the loop never started. That circuit's SIG input saw no edges (a 566 square
DC-coupled, #610); with edges at SIG, phase comparator II drives the filter up from 0 V and the
loop locks. Expected: VDD 10, R1 100k, C1 1 nF, R2 open, PC2 -> 10k -> 100 nF -> VCO IN,
VCO OUT -> COMP IN, 2 kHz at SIG: VCO OUT at 2 kHz (within 1 %) after 50 ms.
"""
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "VDD", "--at", "200,-120", "Voltage=10")
    c("comp", "add", "Pll4046", "--label", "U", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-120,40", "Resistance=100k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-120,0", "Capacitance=1n")
    c("comp", "add", "Resistor", "--label", "RF", "--at", "120,40", "Resistance=10k")
    c("comp", "add", "Capacitor", "--label", "CF", "--at", "120,80", "Capacitance=100n")
    c("comp", "add", "Clock", "--label", "CK", "--at", "-200,-80", "Freq=2kHz", "Voltage=10", "Always_On=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "100,160")
    c("comp", "add", "FreqMeter", "--label", "F", "--at", "300,0")
    for a, b in (("VDD.outnod", "U.vdd"), ("U.vss", "G.Gnd"), ("U.r1", "R1.rPin"), ("R1.lPin", "G.Gnd"),
                 ("U.c1a", "C1.rPin"), ("U.c1b", "C1.lPin"), ("U.inh", "G.Gnd"), ("CK.outnod", "U.sig"),
                 ("U.vcoout", "U.comp"), ("U.pc2", "RF.lPin"), ("RF.rPin", "CF.lPin"), ("CF.rPin", "G.Gnd"),
                 ("RF.rPin", "U.vcoin"), ("U.vcoout", "F.lPin")):
        c("wire", "add", a, b)
    c("sim", "run", "--for", "50ms")
    sim.check("VCO locked to 2 kHz (Hz)", c("read", "F")["value"], 2000, tol=0.01)
    c("sim", "stop")


run_test(test)
