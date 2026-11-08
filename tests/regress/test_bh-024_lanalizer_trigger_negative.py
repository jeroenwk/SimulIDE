"""BH-024: a LAnalizer with a negative Trigger crashes SimulIDE on the first run.

LAnalizer::setTrigger (src/gui/circuitwidget/components/meters/logicanalizer.cpp:272)
stores any int. LAnalizer::updateStep (line ~129) then does
m_channel[m_trigger] for every m_trigger < 9 - with -1 that indexes before the
8-entry array and the program dies. Valid values: 0..7 channel, 8 condition, 9 none.
Right result: an invalid Trigger is ignored and the simulation keeps running.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "LAnalizer", "--label", "L", "--at", "0,0")
    sim.cli("comp", "set", "L", "Trigger=-1")
    try:
        sim.run(0.02)
        sim.cli("ping")
    except Exception as e:
        if "lost connection" in str(e) or "no JSON reply" in str(e):
            sim.check("SimulIDE survives LAnalizer Trigger=-1 + run", 0, 1, tol=0)
            return
        raise
    sim.check("SimulIDE survives LAnalizer Trigger=-1 + run", 1, 1, tol=0)


run_test(test)
