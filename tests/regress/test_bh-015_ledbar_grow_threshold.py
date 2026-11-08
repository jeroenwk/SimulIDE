"""BH-015: LEDs added to a LedBar by growing Size lose the bar's Forward Voltage.

LedBar::createLeds copies Threshold to the new LEDs and then calls
setColorStr(), which (LedBase::setColorStr) resets the threshold to the colour's
default (Yellow 2.4 V). So after Threshold=2.5 V and Size 8 -> 16, LEDs 8..15
still conduct at 2.4 V while the bar reports 2.5 V. Right result: every LED
of the bar has the same forward voltage (here 2.5 V + I*0.6 ohm).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "LedBar", "--label", "L", "--at", "0,0")
    sim.cli("comp", "set", "L", "Threshold=2.5")
    sim.cli("comp", "set", "L", "Size=16")
    pins = sim.cli("comp", "get", "L")["pins"]
    for n, i in enumerate((0, 15)):
        sim.cli("comp", "add", "Resistor", "--label", "R%d" % i, "--at", "-100,%d" % (i * 20), "Resistance=100")
        sim.cli("comp", "add", "Rail", "--label", "V%d" % i, "--at", "-300,%d" % (i * 20), "Voltage=5")
        sim.cli("comp", "add", "Ground", "--label", "G%d" % i, "--at", "300,%d" % (i * 20))
        sim.cli("wire", "add", "V%d.outnod" % i, "R%d.lPin" % i)
        sim.cli("wire", "add", "R%d.rPin" % i, pins[2 * i]["ref"])
        sim.cli("wire", "add", pins[2 * i + 1]["ref"], "G%d.Gnd" % i)
    sim.run("10ms")
    # anode voltage = Vf + I*0.6 ohm, I = (5-anode)/100
    for i in (0, 15):
        va = sim.read("R%d" % i)["pins"]["rPin"]
        sim.check("LED %d anode voltage (Vf 2.5 V)" % i, va, 2.5 + 0.6 * (5 - va) / 100, tol=0.001)


run_test(test)
