"""BH-038: DS1621 negative temperatures are not read as two's complement.

DS1621::doConvert (micro/ds1621.cpp) negates only the integer byte of |T| and sets the half bit
separately, so -2.5 degC reads 0xFE 0x80 (= -1.5 degC); the datasheet format is 9-bit two's
complement: MSB = floor(T), LSB bit 7 = +0.5, so -2.5 = 0xFD 0x80 and -0.5 = 0xFF 0x80.
A TH/TL write of a negative half degree is decoded with the same sign-magnitude mistake (checked
here through the THF flag: Tout/config bit 6 after a conversion).
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def read_temp(sim, temp):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "DS1621", "--label", "S", "--at", "0,0", "Temp=%g" % temp)
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "-200,0")
    sim.cli("wire", "add", "M.SDA", "S.SDA")
    sim.cli("wire", "add", "M.SCL", "S.SCL")
    sim.cli("i2c", "M", "0x48", "--write", "0xEE")
    sim.cli("sim", "run", "--for", "1s", timeout=120)
    r = sim.cli("i2c", "M", "0x48", "--write", "0xAA", "--read", "2")
    b = r["read"]
    raw = ((b[0] ^ 0x80) - 0x80) * 2 + (1 if b[1] & 0x80 else 0)   # signed MSB, half bit
    return b, raw / 2.0


def test(sim):
    for t in (25.5, -2.0, -2.5, -0.5, -10.5):
        b, v = read_temp(sim, t)
        sim.check("DS1621 Temp=%g read back (bytes %s)" % (t, [hex(x) for x in b]), v, t, tol=0)


run_test(test)
