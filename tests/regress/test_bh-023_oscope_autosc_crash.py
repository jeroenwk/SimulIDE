"""BH-023: an Oscope with an AutoSC value outside 0..4 crashes SimulIDE.

Oscope::setAutoSC (src/gui/circuitwidget/components/meters/oscope.cpp:296) calls
OscWidget::setAuto (oscwidget.cpp:338): autoGroup->button(ch)->setChecked(true).
The auto-scale group has the ids 0..4 only (4 = none), so button(-1) or button(5)
is nullptr and the program dies (same class as BH-022).
Right result: an invalid AutoSC is ignored, the program keeps running.
"""
import os, re, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Oscope", "--label", "O", "--at", "0,0")
    for v in (-1, 5, 99):
        try:
            sim.cli("comp", "set", "O", "AutoSC=%d" % v)
            sim.cli("ping")
        except Exception as e:
            if "lost connection" in str(e) or "no JSON reply" in str(e):
                sim.check("SimulIDE survives AutoSC=%d" % v, 0, 1, tol=0)
                return
            raise
        path = os.path.join(tempfile.mkdtemp(), "osc.sim1")
        sim.cli("circuit", "save", path)
        a = int(re.search(r'AutoSC="(-?\d+)"', open(path).read()).group(1))
        sim.check("AutoSC after AutoSC=%d is in 0..4" % v, 0 <= a <= 4, True)


run_test(test)
