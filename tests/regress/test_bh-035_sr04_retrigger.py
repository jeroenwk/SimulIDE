"""BH-035: an HC-SR04 triggered again while its echo is running ends up with Echo stuck high.

SR04::voltChanged (micro/sr04.cpp) stores m_echouS and schedules the echo start 200 us later for
every accepted trigger, and SR04::runEvent uses the same m_echouS flag to tell "start" from "end":
a trigger that arrives before the previous echo ended makes the end event look like a start.
Right result (the real sensor ignores triggers while it measures): the echo pulse ends; with
a 1 kHz trigger and 0.5 m range (2907 us echo) the Echo line is high ~80 % of the time, never 100 %.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "SR04", "--label", "S", "--at", "0,0")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-200,0", "Voltage=0.5", "Out=true")
    sim.cli("comp", "add", "Wave Generator", "--label", "C", "--at", "-200,100", "Wave_Type=Square",
            "Freq=1000", "Duty=2", "Semi_Ampli=2.5", "Mid_Volt=2.5", "Steps=1000")
    sim.cli("comp", "add", "Resistor", "--label", "R", "--at", "200,100", "Resistance=1M")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "300,100")
    sim.cli("wire", "add", "V.outnod", "S.inpin")
    sim.cli("wire", "add", "C.outnod", "S.trigpin")
    sim.cli("wire", "add", "S.outpin", "R.lPin")
    sim.cli("wire", "add", "R.rPin", "G.Gnd")
    sim.cli("comp", "set", "C", "Always_On=true")
    sim.cli("sim", "run", "--for", "20ms", timeout=60)
    a = sim.cli("sim", "average", "--for", "50ms", "S.outpin")["channels"][0]["voltage"]
    duty = a["mean"] / 5.0
    sim.check("SR04 echo duty with 1 kHz triggers (not stuck high; mean %.3f V)" % a["mean"],
              1 if 0.3 < duty < 0.95 else 0, 1, tol=0)


run_test(test)
