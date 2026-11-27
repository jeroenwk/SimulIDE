"""An SCR that conducts stays on through a large gate pulse (create-circuit #497).

A UJT relaxation oscillator (47 k, 4.7 uF, 2N2646) dumps C1 into RB1 100 ohm and the gate.
While the UJT fires, the solver passes through iterates far from the step's solution (the gate
at -49 V in one of them), and the SCR took its latch off from such an iterate: the anode blipped
to the 6 V rail for ~55 us at every gate pulse after the first. A real SCR cannot be turned off
by its gate. Expected: after the first pulse fires it, the SCR reports no OFF up to the third
pulse, and the anode stays low (the 100 ohm load carries ~53 mA).
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "V10", "--at", "-200,-120", "Voltage=10")
    c("comp", "add", "Rail", "--label", "V6", "--at", "200,-120", "Voltage=6")
    c("comp", "add", "Resistor", "--label", "RT", "--at", "-200,-40", "Resistance=47k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-200,80", "Capacitance=4.7uF")
    c("comp", "add", "UJT", "--label", "Q1", "--at", "-80,0", "Model=2N2646")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "-80,-80", "Resistance=100")
    c("comp", "add", "Resistor", "--label", "RB1", "--at", "-80,120", "Resistance=100")
    c("comp", "add", "SCR", "--label", "S1", "--at", "100,40", "GateRes=100", "TrigCurr=10mA", "HoldCurr=8.2mA")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "200,-40", "Resistance=100")
    for g in ("G1", "G2", "G3"):
        c("comp", "add", "Ground", "--label", g, "--at", "0,200")
    for a, b in (("V10.outnod", "RT.lPin"), ("RT.rPin", "Q1.emitter"), ("Q1.emitter", "C1.lPin"),
                 ("C1.rPin", "G1.Gnd"), ("V10.outnod", "R2.lPin"), ("R2.rPin", "Q1.base2"),
                 ("Q1.base1", "RB1.lPin"), ("RB1.rPin", "G2.Gnd"), ("Q1.base1", "S1.gPin"),
                 ("V6.outnod", "RL.lPin"), ("RL.rPin", "S1.lPin"), ("S1.rPin", "G3.Gnd")):
        c("wire", "add", a, b)
    c("sim", "run", "--for", "1200ms")
    events = c("status", "S1")["history"]
    states = [e["text"] for e in events]
    sim.log("S1 states: %s" % ["%s@%s" % (e["text"], e["time"]) for e in events][:12])
    first_on = states.index("ON") if "ON" in states else None
    sim.check("SCR fired", int(first_on is not None), 1, absolute=0)
    offs = states[first_on:].count("OFF") if first_on is not None else -1
    sim.check("OFF reports after it fired", offs, 0, absolute=0)
    sim.check("load current", c("probe", "RL.lPin")[0]["current"], 0.0534, tol=0.03)
    c("sim", "stop")


run_test(test)
