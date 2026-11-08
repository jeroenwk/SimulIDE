"""BH-019: Function part: the saved Functions list grows on every save + reload.

Function::setFunctions replaces the list with the saved one (one entry per output) while
the part still has 1 output, then setNumOutputs appends one empty entry per added output.
A part with 3 outputs saved as "a,b,c" reloads as "a,b,c,,", and again two more per cycle.
A function that was removed by shrinking the outputs then comes back when they grow again.
Right result: a saved circuit reloads with the same Functions string.
"""
import os, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def props(sim):
    return {x["name"]: x["value"] for x in sim.cli("comp", "get", "F")["properties"]}


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Function", "--label", "F", "--at", "0,0")
    sim.cli("comp", "set", "F", "Num_Outputs=3")
    sim.cli("comp", "set", "F", "Functions=i0,i1,i0 | i1")
    before = props(sim)["Functions"]
    sim.check("Functions before save entries", before.count(","), 2, tol=0)
    path = os.path.join(tempfile.mkdtemp(), "func.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("circuit", "open", path)
    sim.check("Functions after reload entries", props(sim)["Functions"].count(","), 2, tol=0)
    # shrink to 2 outputs then grow again: output 2 must be empty, not its old function
    sim.cli("comp", "set", "F", "Num_Outputs=2")
    sim.cli("comp", "set", "F", "Num_Outputs=3")
    sim.check("removed function does not return", props(sim)["Functions"].split(",")[2].strip() == "", True)


run_test(test)
