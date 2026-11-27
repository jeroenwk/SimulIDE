"""555 astable frequency against the control voltage follows the textbook formula
(create-circuit #545, guard).

A PLL build locked 1 kHz at CV 2.634 V where T = C[(R1+R2) ln((Vcc-CV/2)/(Vcc-CV)) + R2 ln2]
puts it at 2.51 V, and called the 555 3 % off. Driven from a stiff source on the CV pin the
model sits on the formula within 0.2 % at every CV; the build's CV was its loop filter's node,
loaded by the internal 5k/5k/5k divider. R1 2.4k, R2 8.2k, C 100 nF, Vcc 5 V:
CV open (10/3 V) 767.4 Hz, 2.0 V 1145 Hz, 2.51 V 999.0 Hz, 2.634 V 963.9 Hz, 3.0 V 860.9 Hz.
"""
import math
from harness import run_test


def test(sim):
    c = sim.cli
    for cv in (None, 2.0, 2.51, 2.634, 3.0):
        c("circuit", "new", "--discard")
        c("comp", "add", "Rail", "--label", "V", "--at", "-200,-100", "Voltage=5")
        c("comp", "add", "Lm555", "--label", "U", "--at", "0,0")
        c("comp", "add", "Resistor", "--label", "R1", "--at", "120,-80", "Resistance=2400")
        c("comp", "add", "Resistor", "--label", "R2", "--at", "120,-20", "Resistance=8200")
        c("comp", "add", "Capacitor", "--label", "C", "--at", "120,60", "Capacitance=100n")
        c("comp", "add", "Ground", "--label", "G", "--at", "0,120")
        c("comp", "add", "FreqMeter", "--label", "F", "--at", "200,0")
        for a, b in (("V.outnod", "U.ePin7"), ("V.outnod", "U.ePin3"), ("V.outnod", "R1.lPin"),
                     ("R1.rPin", "U.ePin6"), ("U.ePin6", "R2.lPin"), ("R2.rPin", "U.ePin5"),
                     ("U.ePin5", "U.ePin1"), ("U.ePin5", "C.lPin"), ("C.rPin", "G.Gnd"),
                     ("U.ePin0", "G.Gnd"), ("U.ePin2", "F.lPin")):
            c("wire", "add", a, b)
        if cv is not None:
            c("comp", "add", "Rail", "--label", "VC", "--at", "-200,100", "Voltage=%g" % cv)
            c("wire", "add", "VC.outnod", "U.ePin4")
        c("sim", "run", "--for", "50ms")
        f = c("read", "F")["value"]
        v = cv if cv is not None else 10 / 3
        T = 100e-9 * ((2400 + 8200) * math.log((5 - v / 2) / (5 - v)) + 8200 * math.log(2))
        sim.check("f at CV %s (Hz)" % ("open" if cv is None else "%g V" % cv), f, 1 / T, tol=0.003)
        c("sim", "stop")


run_test(test)
