"""BH-048: Frequency 0 or negative on an I2C part hangs the simulation.

TwiModule::setFreqKHz (src/microsim/modules/twi/twimodule.cpp:349) computes
m_clockPeriod = 1e12/m_freq/2 without a check: 0 gives an infinite period, a negative one a
negative period (wraps as unsigned), and the next bus event loop never advances time.
Right result: the run finishes (the frequency is ignored or clamped).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test, SimHang


def runs(sim, part, f):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", part, "--label", "D", "--at", "0,0")
    try:
        sim.cli("comp", "set", "D", "Frequency=%s" % f)
        sim.cli("sim", "run", "--for", "2ms", timeout=30)
    except SimHang:
        return 0
    return 1


def test(sim):
    for part in ("I2CMaster", "I2CRam"):
        for f in ("100", "0", "-1"):
            sim.check("%s Frequency=%s: run finishes" % (part, f), runs(sim, part, f), 1, tol=0)


run_test(test)
