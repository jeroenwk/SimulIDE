"""BH-018: Touch pad (resistive) RxMin/RyMin are lost on save + reload when above the default RxMax/RyMax (500 ohm).

TouchPad::setRxMin / setRyMin ignore a value larger than the current Max and
the circuit loader sets Min before Max, so a pad saved with RxMin=700,
RxMax=900 reloads with RxMin=100 (same for Ry).
Right result: a saved circuit reloads with the same property values.
"""
import os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def props(sim):
    return {x["name"]: x["value"] for x in sim.cli("comp", "get", "T")["properties"]}


def num(s):
    return float(s.split()[0])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "TouchPadR", "--label", "T", "--at", "0,0")
    for p, v in (("RxMax", 900), ("RxMin", 700), ("RyMax", 1900), ("RyMin", 1700)):
        sim.cli("comp", "set", "T", "%s=%d" % (p, v))
    sim.check("RxMin before save", num(props(sim)["RxMin"]), 700, tol=0)
    path = os.path.join(tempfile.mkdtemp(), "touch.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("circuit", "open", path)
    p = props(sim)
    for name, v in (("RxMin", 700), ("RxMax", 900), ("RyMin", 1700), ("RyMax", 1900)):
        sim.check(name + " after reload", num(p[name]), v, tol=0)


run_test(test)
