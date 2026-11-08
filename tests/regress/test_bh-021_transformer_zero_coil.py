"""BH-021: changing a Transformer winding from "1,0" to "1,1" crashes SimulIDE.

Transformer::getLeght builds the winding topology counting coils equal to "0",
but create()/modify() skip them. "1,0" and "1,1" then share the topology "c,c",
so createCoils() calls modify() on a winding that was created with one pin fewer.
Right result: no crash and the winding ends with 2 coils (pins: 3 for "1,1").
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Transformer", "--label", "T", "--at", "0,0")
    sim.cli("comp", "set", "T", "Secondary=1,0")
    npins0 = len(sim.cli("comp", "get", "T")["pins"])
    sim.cli("comp", "set", "T", "Secondary=1,1")
    sec = {x["name"]: x["value"] for x in sim.cli("comp", "get", "T")["properties"]}["Secondary"]
    sim.check("Secondary kept", sec == "1,1", True, tol=0)
    sim.check("pin count grows by one", len(sim.cli("comp", "get", "T")["pins"]) - npins0, 1, tol=0)


run_test(test)
