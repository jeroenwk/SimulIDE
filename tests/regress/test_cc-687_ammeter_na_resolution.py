"""An Amperimeter resolves nA on a node at several volts (create-circuit #687).

The meter is a 1 uOhm resistor and read its current as dV/R: at 9 V the last bit of dV is
about 2e-15 V, so the reading moved in steps of about 2.2e-10 A per volt (a 1.86 nA photodiode
dark current read 1.776 nA). Expected: a battery through the meter into 5 GOhm reads V / 5 GOhm
within 0.1 %, at 1, 9 and 20 V; and 1 A through 10 ohm still reads 1 A (the meter stays
ideal).
"""

from harness import run_test


def test(sim):
    c = sim.cli
    for v, r in ((1, 5e9), (9, 5e9), (20, 5e9), (10, 10)):
        c("circuit", "new", "--discard")
        c("comp", "add", "Battery", "--label", "B", "--at", "0,0", "Voltage=%g" % v)
        c("comp", "add", "Amperimeter", "--label", "M", "--at", "100,-60")
        c("comp", "add", "Resistor", "--label", "R", "--at", "200,0", "Resistance=%g" % r)
        c("comp", "add", "Ground", "--label", "G", "--at", "0,100")
        for a, b in (("B.lPin", "M.lPin"), ("M.rPin", "R.lPin"), ("R.rPin", "G.Gnd"), ("B.rPin", "G.Gnd")):
            c("wire", "add", a, b)
        c("sim", "run", "--for", "1ms")
        sim.check("%g V into %g ohm: meter (A)" % (v, r), c("read", "M")["value"], v / r, tol=0.001)
        c("sim", "stop")


run_test(test)
