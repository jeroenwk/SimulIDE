"""BH-022: an Oscope with a Tracks value other than 1, 2 or 4 crashes SimulIDE.

OscWidget::setTracks (src/gui/dataplotwidget/oscwidget.h:40) does
trackGroup->button(tr)->setChecked(true); the button group only has ids 1, 2
and 4, so button(tr) returns nullptr for anything else (0, 3, 5, a garbled
"abc" in a hand-edited .sim1) and the null dereference kills the program.
Right result: an invalid Tracks value is ignored or clamped, the program keeps
running and Tracks reads back 1, 2 or 4.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Oscope", "--label", "O", "--at", "0,0")
    for v in (3, 0, 5):
        try:
            sim.cli("comp", "set", "O", "Tracks=%d" % v)
        except Exception as e:
            if "lost connection" in str(e) or "no JSON reply" in str(e):
                sim.check("SimulIDE survives Tracks=%d" % v, 0, 1, tol=0)
                return
        p = {x["name"]: x["value"] for x in sim.cli("comp", "get", "O")["properties"]}
        sim.check("Tracks after Tracks=%d is 1, 2 or 4" % v, int(p["Tracks"]) in (1, 2, 4), True)


run_test(test)
