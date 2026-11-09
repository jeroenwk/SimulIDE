"""BH-041: changing the DS1621 Temperature property during continuous conversion is ignored.

DS1621::runEvent only calls doConvert when m_changed is set; the up/down buttons set it but the
Temp property setter (setTemp, ds1621.h) does not. Start Convert at 25 C, read 25.0, set Temp=30
through the property (a script, the Properties dialog or the CLI), wait for the next 750 ms
conversion: Read Temperature must give 30.0, it keeps giving 25.0.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "DS1621", "--label", "S", "--at", "0,0", "Temp=25")
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "-200,0")
    sim.cli("wire", "add", "M.SDA", "S.SDA")
    sim.cli("wire", "add", "M.SCL", "S.SCL")
    sim.cli("i2c", "M", "0x48", "--write", "0xEE")
    sim.cli("sim", "run", "--for", "1s", timeout=120)
    r = sim.cli("i2c", "M", "0x48", "--write", "0xAA", "--read", "2")
    sim.check("before: MSB", r["read"][0], 25, tol=0)
    sim.cli("comp", "set", "S", "Temp=30")
    sim.cli("sim", "run", "--for", "2s", timeout=120)
    r = sim.cli("i2c", "M", "0x48", "--write", "0xAA", "--read", "2")
    sim.check("after Temp=30: MSB", r["read"][0], 30, tol=0)


run_test(test)
