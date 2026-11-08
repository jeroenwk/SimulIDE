"""BH-016: Seven Segment / LED Matrix: Forward Voltage property and new LEDs disagree with the colour.

LedBase::setColorStr sets the LED threshold to the colour's default (Red 1.8 V).
(a) SevenSegment::setColorStr / LedMatrix::setColorStr do not update their stored
    m_threshold, so after Color=Red the Forward Voltage property still reads
    2.4 V while the segments conduct at 1.8 V.
(b) the same for the LED Matrix, and when its Rows grow the new LEDs must
    keep the set Forward Voltage.
Right result: the property equals what the LEDs do, and every LED of the
part uses the same Forward Voltage (2.5 V + I*R here).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def prop(sim, c, name):
    return {x["name"]: x["value"] for x in sim.cli("comp", "get", c)["properties"]}[name]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Seven Segment", "--label", "S", "--at", "0,0")
    sim.cli("comp", "set", "S", "Color=Red")
    thr = float(prop(sim, "S", "Threshold").split()[0])
    sim.check("Forward Voltage property after Color=Red", thr, 1.8, tol=0.001)
    sim.cli("comp", "set", "S", "Threshold=2.5")
    sim.cli("comp", "set", "S", "NumDisplays=2")
    sim.cli("comp", "add", "Resistor", "--label", "R", "--at", "-100,0", "Resistance=100")
    sim.cli("comp", "add", "Rail", "--label", "V", "--at", "-300,0", "Voltage=5")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "300,0")
    sim.cli("wire", "add", "V.outnod", "R.lPin")
    sim.cli("wire", "add", "R.rPin", "S.pin_a")
    sim.cli("wire", "add", "S.pin_commonb", "G.Gnd")
    sim.run("10ms")
    va = sim.read("R")["pins"]["rPin"]
    sim.check("display 2 segment a anode (Vf 2.5 V)", va, 2.5 + 1.0 * (5 - va) / 100, tol=0.001)

    # LED Matrix
    sim.cli("comp", "add", "LedMatrix", "--label", "M", "--at", "0,300", "Rows=2", "Cols=2")
    sim.cli("comp", "set", "M", "Color=Red")
    thr = float(prop(sim, "M", "Threshold").split()[0])
    sim.check("matrix Forward Voltage property after Color=Red", thr, 1.8, tol=0.001)
    sim.cli("comp", "set", "M", "Threshold=2.5")
    sim.cli("comp", "set", "M", "Rows=3")
    sim.check("matrix Color kept when Rows change", float(prop(sim, "M", "Color") == "Red"), 1.0, tol=0)
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "-100,300", "Resistance=100")
    sim.cli("comp", "add", "Rail", "--label", "V2", "--at", "-300,300", "Voltage=5")
    sim.cli("comp", "add", "Ground", "--label", "G2", "--at", "300,300")
    sim.cli("wire", "add", "V2.outnod", "R2.lPin")
    sim.cli("wire", "add", "R2.rPin", "M.pinRow2")
    sim.cli("wire", "add", "M.pinCol0", "G2.Gnd")
    sim.run("10ms")
    va = sim.read("R2")["pins"]["rPin"]
    sim.check("matrix row 3 LED anode (Vf 2.5 V)", va, 2.5 + 0.6 * (5 - va) / 100, tol=0.001)


run_test(test)
