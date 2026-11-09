"""BH-039: DS1307 12-hour mode reads wrong hour registers.

DS1307 datasheet: hours register bit 6 = 12 hour mode, bit 5 = PM (12 h) / 20 h (24 h), bits 4-0
BCD hour 1-12. DS1307::writeByte (micro/ds1307.cpp) only subtracts 12 from an hour above 12: no PM
bit, midnight reads hour 0, noon reads 12 without PM.
Set the clock (Time_Updtd false) to each hour in 12 h mode, read the hours register back at once.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test

# register written -> register expected back (12 h mode, bit 6 set)
CASES = [(0x52, 0x52, "12 AM (midnight)"), (0x41, 0x41, "1 AM"), (0x72, 0x72, "12 PM (noon)"),
         (0x61, 0x61, "1 PM"), (0x71, 0x71, "11 PM")]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "0,0")
    sim.cli("comp", "add", "DS1307", "--label", "R", "--at", "200,0", "Time_Updtd=false")
    sim.cli("wire", "add", "M.SDA", "R.SDA")
    sim.cli("wire", "add", "M.SCL", "R.SCL")
    for w, exp, name in CASES:
        sim.cli("i2c", "M", "0x68", "--write", "2,%d" % w)
        r = sim.cli("i2c", "M", "0x68", "--write", "2", "--read", "1")
        sim.check("hours register after writing 0x%02X (%s)" % (w, name), r["read"][0], exp, tol=0)
    sim.cli("i2c", "M", "0x68", "--write", "2,0x23")          # 24 h mode must stay as it was
    r = sim.cli("i2c", "M", "0x68", "--write", "2", "--read", "1")
    sim.check("24 h mode 23 h", r["read"][0], 0x23, tol=0)


run_test(test)
