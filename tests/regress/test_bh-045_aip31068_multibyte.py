"""BH-045: Aip31068_i2c ignores every data byte after the first of a transfer.

The control byte has Co (0x80) and RS (0x40). Co=0: all following bytes are of that type;
Co=1: one byte, then a new control byte. Aip31068_i2c::readByte goes phase 0 (control), 1 (one
byte), 2 (ignores the rest), so `0x00,0x38,0x0C,0x01` runs only 0x38 and the display stays off.
Compare the picture after commands/chars sent one transfer each with the same sent in one transfer.
"""
import hashlib, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def picture(sim):
    sim.cli("sim", "run", "--for", "5ms")
    r = sim.cli("capture", "circuit", "--out", "x")
    return hashlib.md5(open(r["file"], "rb").read()).hexdigest()


def build(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Aip31068_i2c", "--label", "D", "--at", "0,0")
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "-200,0")
    sim.cli("wire", "add", "M.SDA", "D.SDA")
    sim.cli("wire", "add", "M.SCL", "D.SCL")
    return lambda s: sim.cli("i2c", "M", "0x3E", "--write", s)


def test(sim):
    w = build(sim)
    blank = picture(sim)
    for s in ["0x00,0x38", "0x00,0x0C", "0x00,0x01", "0x40,0x41", "0x40,0x42"]:
        w(s)
    ref = picture(sim)
    sim.check("control: separate transfers show text", int(ref != blank), 1, tol=0)

    w = build(sim)
    w("0x00,0x38,0x0C,0x01")
    w("0x40,0x41,0x42")
    sim.check("Co=0: several bytes in one transfer", int(picture(sim) == ref), 1, tol=0)

    w = build(sim)
    w("0x80,0x38,0x80,0x0C,0x80,0x01,0x40,0x41,0x40,0x42")
    sim.check("Co=1: control byte before every byte", int(picture(sim) == ref), 1, tol=0)


run_test(test)
