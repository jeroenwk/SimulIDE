"""BH-040: DS1307 year register after 31 Dec 2099 reads 0x9A.

The DS1307 year register is BCD 00-99. DS1307::bcdToDec(char) is signed, so writing year 0x99
(any register value >= 0x80) is decoded as a negative number: the year is stored as 1933. DS1307::writeByte (micro/ds1307.cpp) gives
decToBcd(year - 2000), so 2100 gives 100 -> 0x9A. Set 31 Dec 2099 23:59:59 (Time_Updtd false), run
2 s, the registers must read year 0x00, month 0x01, date 0x01.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "0,0")
    sim.cli("comp", "add", "DS1307", "--label", "R", "--at", "200,0", "Time_Updtd=false")
    sim.cli("wire", "add", "M.SDA", "R.SDA")
    sim.cli("wire", "add", "M.SCL", "R.SCL")
    sim.cli("i2c", "M", "0x68", "--write", "0,0x00,0x00,0x12,0x07,0x31,0x12,0x99")
    r = sim.cli("i2c", "M", "0x68", "--write", "6", "--read", "1")
    sim.check("year 0x99 reads back", r["read"][0], 0x99, tol=0)
    sim.cli("i2c", "M", "0x68", "--write", "0,0x59,0x59,0x23,0x07,0x31,0x12,0x99")
    sim.cli("sim", "run", "--for", "2s", timeout=120)
    r = sim.cli("i2c", "M", "0x68", "--write", "4", "--read", "3")
    sim.log("date, month, year: %s" % r["read"])
    sim.check("date", r["read"][0], 0x01, tol=0)
    sim.check("month", r["read"][1], 0x01, tol=0)
    sim.check("year BCD", r["read"][2], 0x00, tol=0)


run_test(test)
