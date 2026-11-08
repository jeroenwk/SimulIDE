"""BH-025: a Mosfet with Threshold above 10 V drives its drain far above the supply.

eMosfet::updateValues / setThreshold (src/simulator/elements/active/e-mosfet.cpp) compute
m_kRDSon = RDSon*(10-Threshold). For Threshold >= 10 that is zero or negative, the channel
current formula changes sign and the model injects more current than Vds/RDSon: a 10 V supply
with a 100 ohm drain resistor shows hundreds of volts on the drain.
Right result: the drain stays between 0 V and the supply (10 V); with the gate far above the
threshold (Vgs 20 V) the transistor is on: Vd = 10 V * 1/(100+1) = 0.099 V.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def drain(sim, vg, th):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "VD", "--at", "-200,0", "Voltage=10", "Out=true")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "VG", "--at", "-200,200", "Voltage=%s" % vg, "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-200,100")
    sim.cli("comp", "add", "Resistor", "--label", "R", "--at", "-100,0", "Resistance=100")
    sim.cli("comp", "add", "Mosfet", "--label", "M", "--at", "0,0")
    sim.cli("comp", "set", "M", "Threshold=%s" % th)
    sim.cli("wire", "add", "VD.outnod", "R.lPin")
    sim.cli("wire", "add", "R.rPin", "M.Dren")
    sim.cli("wire", "add", "M.Sour", "G.Gnd")
    sim.cli("wire", "add", "VG.outnod", "M.Gate")
    sim.run("5ms")
    return sim.read("M")["pins"]["Dren"]


def test(sim):
    for th, vg in ((8, 20), (11, 20), (11, 16), (15, 20)):
        sim.check("Mosfet Threshold=%g Vg=%g: drain 10V/100ohm/RDSon 1 ohm" % (th, vg),
                  drain(sim, vg, th), 10.0 / 101, tol=0, absolute=0.05)


run_test(test)
