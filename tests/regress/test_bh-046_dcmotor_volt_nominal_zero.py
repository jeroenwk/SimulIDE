"""BH-046: Dc Motor with Volt_Nominal 0 poisons the shaft angle with NaN for the rest of the run.

DcMotor::updatePos (outputs/motors/dcmotor.cpp) adds (volt / m_voltNom) * dt to m_delta; with
Volt_Nominal 0 that is inf, m_ang = remainder(inf) = NaN, and m_ang is only reset by
initialize() (simulation start). Setting Volt_Nominal back to 5 V in the running circuit must
make the shaft turn again: 5 V on a 60 RPM / 5 V motor for 0.5 s is half a turn, so the linked
Potentiometer (dial 1000..0 per turn) must read about 500 ohm; the buggy code stays at 0.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def dial(sim):
    props = {x["name"]: x["value"] for x in sim.cli("comp", "get", "P")["properties"]}
    return float(props["Value_Ohm"].split()[0])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Dc Motor", "--label", "M", "--at", "0,0", "RPM_Nominal=60", "Volt_Nominal=0")
    sim.cli("comp", "add", "Potentiometer", "--label", "P", "--at", "200,0", "Resistance=1000")
    sim.cli("comp", "link", "M", "P")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-200,0", "Voltage=5", "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "200,100")
    sim.cli("wire", "add", "V.outnod", "M.lPin")
    sim.cli("wire", "add", "M.rPin", "G.Gnd")
    sim.cli("sim", "run", "--for", "50ms", timeout=60)
    sim.cli("comp", "set", "M", "Volt_Nominal=5")
    # the dial restarts from the angle at that moment: measure the change over 0.5 s
    sim.cli("sim", "run", "--for", "50ms", timeout=60)
    a = dial(sim)
    sim.cli("sim", "run", "--for", "500ms", timeout=60)
    b = dial(sim)
    turn = ((a - b) % 1000.0) / 1000.0
    sim.check("half a turn in 0.5 s after Volt_Nominal 0 -> 5 (a=%g b=%g)" % (a, b), turn, 0.5, tol=0.03)


run_test(test)
