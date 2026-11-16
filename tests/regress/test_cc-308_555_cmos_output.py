"""555 CMOS output option (create-circuit #308).

SimulIDE's 555 has a bipolar output stage: its high level is Vcc - 1.3 V. A CMOS 555 (7555,
TLC555), which several book figures call for, swings rail to rail; there was no way to say so.

Right result, Vcc 9 V, trigger held low (output high), 10k load:
default: 7.7 V (unchanged); CMOS=true: 9.0 V; output low stays 0 V either way.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def out(sim, trig_high, props=()):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "VCC", "--at", "200,-100", "Voltage=9", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "-100,100")
    c("comp", "add", "Lm555", "--label", "U", "--at", "0,0", *props)
    c("comp", "add", "Resistor", "--label", "RL", "--at", "-100,0", "--rotate", "90", "Resistance=10k")
    c("wire", "add", "U.ePin0", "G.Gnd")
    c("wire", "add", "U.ePin7", "VCC.outnod")
    c("wire", "add", "U.ePin3", "VCC.outnod")
    c("wire", "add", "U.ePin1", "VCC.outnod" if trig_high else "G.Gnd")
    c("wire", "add", "U.ePin5", "VCC.outnod" if trig_high else "G.Gnd")
    c("wire", "add", "U.ePin2", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    sim.run("1ms")
    return sim.read("RL")["pins"]["lPin"]


def test(sim):
    sim.check("bipolar 555 output high (Vcc - 1.3 V)", out(sim, False), 7.7, tol=0, absolute=0.05)
    sim.check("CMOS 555 output high (Vcc)", out(sim, False, ("CMOS=true",)), 9.0, tol=0, absolute=0.05)
    sim.check("CMOS 555 output low", out(sim, True, ("CMOS=true",)), 0.0, tol=0, absolute=0.05)


run_test(test)
