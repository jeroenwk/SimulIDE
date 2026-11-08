"""BH-027: a Force Strain Gauge wired to a source hangs the simulation (100 % CPU forever).

Strain::initialize (src/gui/circuitwidget/components/passive/resist_sensors/strain.cpp:69) calls
updateStep() directly, which stamps the admittance (eResistor::setRes) while the circuit is
being initialised; the other variable resistors (LDR, RTD, Thermistor) only set m_needUpdate
and let updateStep() run later. The first 'sim run' then never returns.
Right result: 5 V across the gauge (120 ohm at 0 N, 20 C) runs normally and draws 5/120 = 41.7 mA.
"""
import os, sys, subprocess
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-200,0", "Voltage=5", "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-200,100")
    sim.cli("comp", "add", "Strain", "--label", "S", "--at", "0,0")
    sim.cli("wire", "add", "V.outnod", "S.lPin")
    sim.cli("wire", "add", "S.rPin", "G.Gnd")
    try:
        sim.cli("sim", "run", "--for", "2ms", timeout=20)
    except subprocess.TimeoutExpired:
        sim.check("sim run of a Strain gauge circuit returns within 20 s", 0, 1, tol=0)
        return
    sim.check("sim run of a Strain gauge circuit returns within 20 s", 1, 1, tol=0)
    sim.check("Strain current at 0 N, 5 V (mA)", abs(sim.cli("comp", "get", "S")["pins"][0]["current"]) * 1000, 5 / 120 * 1000, tol=0.02)


run_test(test)
