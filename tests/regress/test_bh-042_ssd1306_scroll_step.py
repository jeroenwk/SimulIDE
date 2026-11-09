"""BH-042: SSD1306 horizontal scroll ignores the step-time code (always every 2 frames).

Ssd1306::configScroll (ssd1306.cpp) switches on the "time interval between scroll steps" code
(datasheet: 000 = 5 frames, 001 = 64, 010 = 128, 011 = 256, 100 = 3, 101 = 4, 110 = 25, 111 = 2)
but every case lacks a break, so it falls through to the last one and m_scrollStep is always 2.
Set up a 128x64 SSD1306 on an I2C Master, put a bright block in DDRAM, start a left scroll with
code 3 (256 frames, > 5 s at 50 frames/s): the picture must not move within about a second of
wall time. A positive control with code 7 (2 frames) must move.
"""
import hashlib, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test

FILL = ",".join(["0xFF"] * 8 + ["0x00"] * 8)


def picture(sim):
    r = sim.cli("capture", "circuit", "--out", "x")
    return hashlib.md5(open(r["file"], "rb").read()).hexdigest()


def moves(sim, code):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Ssd1306", "--label", "D", "--at", "0,0")
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "-200,0")
    sim.cli("wire", "add", "M.SDA", "D.PinSda")
    sim.cli("wire", "add", "M.SCL", "D.PinSck")
    sim.cli("i2c", "M", "0x3C", "--write", "0x00,0xAF")        # display on
    sim.cli("i2c", "M", "0x3C", "--write", "0x00,0x20,0x00")   # horizontal addressing
    sim.cli("i2c", "M", "0x3C", "--write", "0x40," + FILL)     # 8 bright columns, page 0
    sim.cli("i2c", "M", "0x3C", "--write",
            "0x00,0x27,0x00,0x00,%d,0x00,0x00,0xFF,0x2F" % code)  # left scroll, step code, on
    sim.cli("sim", "run", "--for", "10ms", timeout=60)
    before = picture(sim)
    sim.cli("sim", "start")
    time.sleep(1.0)
    sim.cli("sim", "pause")
    return picture(sim) != before


def test(sim):
    sim.check("code 7 (2 frames) scrolls", int(moves(sim, 7)), 1, tol=0)
    sim.check("code 3 (256 frames) does not scroll within 1 s", int(moves(sim, 3)), 0, tol=0)


run_test(test)
