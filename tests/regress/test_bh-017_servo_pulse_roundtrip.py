"""BH-017: Servo MinPulse/MaxPulse are lost on save + reload when MinPulse >= the default MaxPulse.

Servo::setMinPulse ignores a value >= the current MaxPulse (default 2000 us) and
the circuit loader sets MinPulse before MaxPulse, so a servo saved with
MinPulse=2500, MaxPulse=3500 reloads with MinPulse=1000.
Right result: a saved circuit reloads with the same property values.
"""
import os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def props(sim):
    return {x["name"]: x["value"] for x in sim.cli("comp", "get", "S")["properties"]}


def num(s):
    return float(s.split()[0])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Servo", "--label", "S", "--at", "0,0")
    sim.cli("comp", "set", "S", "MaxPulse=3500")
    sim.cli("comp", "set", "S", "MinPulse=2500")
    sim.check("MinPulse before save", num(props(sim)["MinPulse"]), 2500, tol=0)
    path = os.path.join(tempfile.mkdtemp(), "servo.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("circuit", "open", path)
    p = props(sim)
    sim.check("MinPulse after reload", num(p["MinPulse"]), 2500, tol=0)
    sim.check("MaxPulse after reload", num(p["MaxPulse"]), 3500, tol=0)


run_test(test)
