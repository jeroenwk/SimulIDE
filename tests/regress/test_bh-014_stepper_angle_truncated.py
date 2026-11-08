"""BH-014: Stepper shaft angle is truncated to an integer per half step.

Stepper::m_stpang is an int: 360*8/Steps (2880/200 = 14.4 for a 200 steps/rev
motor) is truncated to 14, so the shaft turns 14*400 = 5600 of the 5760
sixteenth-degrees per revolution (2.8 % short). A 200-step motor driven through
100 full steps must be at half a turn: the linked Potentiometer (dial 0..1000
= 1000..0 over one turn, see Stepper::updateStep) then reads 500 of 1000 ohm
(tolerance 1 % for the 1-ohm dial step); the buggy code gives about 514.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Stepper", "--label", "M", "--at", "0,0", "Bipolar=true", "Steps=200")
    sim.cli("comp", "add", "Potentiometer", "--label", "P", "--at", "200,0", "Resistance=1000")
    sim.cli("comp", "link", "M", "P")
    for i, p in enumerate(["PinA1", "PinA2", "PinB1", "PinB2"]):
        sim.cli("comp", "add", "Rail", "--label", "R%d" % i, "--at", "-200,%d" % (i * 40), "Voltage=0")
        sim.cli("wire", "add", "R%d.outnod" % i, "M." + p)
    phases = [(5, 0), (0, 5), (-5, 0), (0, -5)]

    def setph(a, b):
        for i, v in enumerate([max(a, 0), max(-a, 0), max(b, 0), max(-b, 0)]):
            sim.cli("comp", "set", "R%d" % i, "Voltage=%s" % v)
        sim.run("2ms")

    setph(*phases[0])
    for k in range(1, 101):
        setph(*phases[k % 4])
    props = {x["name"]: x["value"] for x in sim.cli("comp", "get", "P")["properties"]}
    ohm = float(props["Value_Ohm"].split()[0])
    sim.check("pot after 100 of 200 full steps (half a turn)", ohm, 500.0, tol=0.01)


run_test(test)
