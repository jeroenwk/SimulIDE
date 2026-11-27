"""A comparator wired straight to an SCR gate fires a crowbar and it stays on (create-circuit #515).

The MC3423-style crowbar of AoE fig 9.102b without its emitter follower: an opAmp powered from
the protected rail compares the divided rail (3.4 k / 2.61 k) with 2.6 V and drives the gate.
The rail comes from 6.05 V through 1 ohm, over the 5.99 V trip point. Reported with skill 1.22.1:
the output stayed under one diode drop and the loop limit-cycled every ~60 us. Not reproduced
on main a0a2f8e (nor with a 5 V to 6.05 V step), most likely fixed with the gate's reverse
conduction (#496); kept as a guard. Expected: the SCR fires and stays on (no OFF after the
first ON), the rail sits at ~0.94 V and the SCR carries ~5.1 A.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "VIN", "--at", "-240,-160", "Voltage=6.05")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "-160,-160", "Resistance=1")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-80,-80", "Resistance=3.4k")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "-80,40", "Resistance=2.61k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-160,40", "Capacitance=1nF")
    c("comp", "add", "Fixed Voltage", "--label", "VR", "--at", "-80,120", "Voltage=2.6", "Out=true")
    c("comp", "add", "opAmp", "--label", "U1", "--at", "40,0", "Gain=1000", "Out_Imped=40", "Power_Pins=true")
    c("comp", "add", "SCR", "--label", "S1", "--at", "160,40", "GateRes=100", "TrigCurr=10mA", "HoldCurr=8.2mA")
    for g in ("G1", "G2", "G3", "G4"):
        c("comp", "add", "Ground", "--label", g, "--at", "0,240")
    for a, b in (("VIN.outnod", "RS.lPin"), ("RS.rPin", "R1.lPin"), ("R1.rPin", "R2.lPin"),
                 ("R2.rPin", "G1.Gnd"), ("R1.rPin", "C1.lPin"), ("C1.rPin", "G2.Gnd"),
                 ("R1.rPin", "U1.inputNinv"), ("VR.outnod", "U1.inputInv"),
                 ("RS.rPin", "U1.powerPos"), ("U1.powerNeg", "G3.Gnd"),
                 ("U1.output", "S1.gPin"), ("RS.rPin", "S1.lPin"), ("S1.rPin", "G4.Gnd")):
        c("wire", "add", a, b)
    c("sim", "run", "--for", "300us")
    events = c("status", "S1")["history"]
    states = [e["text"] for e in events]
    sim.log("S1 states: %s" % ["%s@%s" % (e["text"], e["time"]) for e in events][:12])
    first_on = states.index("ON") if "ON" in states else None
    sim.check("SCR fired", int(first_on is not None), 1, absolute=0)
    sim.check("OFF reports after it fired", states[first_on:].count("OFF") if first_on is not None else -1, 0, absolute=0)
    sim.check("rail held low", c("probe", "S1.lPin")[0]["voltage"], 0.944, tol=0.05)
    sim.check("crowbar current", c("probe", "S1.lPin")[0]["current"], 5.1, tol=0.03)
    c("sim", "stop")


run_test(test)
