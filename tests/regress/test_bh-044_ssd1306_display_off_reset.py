"""BH-044: SSD1306 Display OFF (0xAE) resets segment remap, scan direction, inverse and addressing mode.

Ssd1306::proccessCommand case 0xAE calls reset(), which also clears m_remap, m_scanInv,
m_dispInv, m_addrMode, the addresses and the scroll set-up. In the datasheet 0xAE only puts the
panel in sleep mode: RAM and every setting are kept, so 0xAE then 0xAF must show the same picture.
Set remap (0xA1) and COM scan inversion (0xC8), draw a pattern, capture it (display on), then send
0xAE, 0xAF and capture again: the picture must be unchanged. A positive control checks that
remap/scan do change the picture.
"""
import hashlib, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test

PAT = ",".join(["0x01", "0x03", "0x07", "0x0F", "0x1F", "0x3F", "0x7F", "0xFF"])


def picture(sim):
    r = sim.cli("capture", "circuit", "--out", "x")
    return hashlib.md5(open(r["file"], "rb").read()).hexdigest()


def build(sim, flip):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Ssd1306", "--label", "D", "--at", "0,0")
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "-200,0")
    sim.cli("wire", "add", "M.SDA", "D.PinSda")
    sim.cli("wire", "add", "M.SCL", "D.PinSck")
    w = lambda s: sim.cli("i2c", "M", "0x3C", "--write", s)
    w("0x00,0x20,0x00")                 # horizontal addressing
    if flip:
        w("0x00,0xA1,0xC8")             # remap + scan inverted
    w("0x40," + PAT)
    w("0x00,0xAF")
    return w


def test(sim):
    w = build(sim, False)
    plain = picture(sim)
    w = build(sim, True)
    flipped = picture(sim)
    sim.check("control: remap+scan change the picture", int(plain != flipped), 1, tol=0)
    w("0x00,0xAE")
    w("0x00,0xAF")
    sim.check("picture unchanged after sleep/wake", int(picture(sim) == flipped), 1, tol=0)


run_test(test)
