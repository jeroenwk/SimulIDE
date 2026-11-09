"""CLI `i2c` + the I2C Master part (bug hunt IMP-023): one I2C transfer from the command line.

An I2C Master drives an I2C RAM (address 0x50, 64 KiB, two address bytes) and a DS1621 (0x48 with
A0-A2 low) on one bus; the master's internal 100 kOhm pull-ups hold the lines up.
- Writing 0xAB 0xCD at RAM address 0x0010, then reading 2 bytes from 0x0010 gives them back.
- An address nobody answers (0x23) gets a NACK and the transfer ends with a STOP.
- DS1621 at 25 C, after Start Convert (0xEE) and the 750 ms conversion: Read Temperature (0xAA)
  gives 0x19 0x00 (25.0 C; DS1621 datasheet, Table 2: +25 C = 0x1900).
"""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "I2CMaster", "--label", "M", "--at", "0,0")
    sim.cli("comp", "add", "I2CRam", "--label", "RAM", "--at", "200,0")
    sim.cli("comp", "add", "DS1621", "--label", "T", "--at", "200,150", "Temp=25")
    sim.cli("wire", "add", "M.SDA", "RAM.SDA")
    sim.cli("wire", "add", "M.SCL", "RAM.SCL")
    sim.cli("wire", "add", "T.SDA", "M.SDA")      # Junctions on the master's wires
    sim.cli("wire", "add", "T.SCL", "M.SCL")

    r = sim.cli("i2c", "M", "0x50", "--write", "0x00,0x10,0xAB,0xCD")
    sim.log("RAM write: %s" % r)
    sim.check("RAM write: ok", int(r["ok"]), 1, absolute=0)
    sim.check("RAM write: every byte ACKed", sum(r["write_ack"]), 4, absolute=0)

    r = sim.cli("i2c", "M", "0x50", "--write", "0x00,0x10", "--read", "2")
    sim.log("RAM read: %s" % r)
    sim.check("RAM read: first byte 0xAB", r["read"][0], 0xAB, absolute=0)
    sim.check("RAM read: second byte 0xCD", r["read"][1], 0xCD, absolute=0)
    sim.check("RAM read: bus log", int(r["log"] == "S A0xA0+ W0x00+ W0x10+ Sr A0xA1+ R0xAB+ R0xCD- P"), 1, absolute=0)

    r = sim.cli("i2c", "M", "0x23", "--write", "1")
    sim.log("no device: %s" % r)
    sim.check("no device at 0x23: NACK", int(r["addr_ack_w"]), 0, absolute=0)
    sim.check("no device at 0x23: not ok", int(r["ok"]), 0, absolute=0)

    sim.cli("i2c", "M", "0x48", "--write", "0xEE")   # Start Convert
    sim.cli("sim", "run", "--for", "1s")
    r = sim.cli("i2c", "M", "0x48", "--write", "0xAA", "--read", "2")
    sim.log("DS1621: %s" % r)
    sim.check("DS1621 25 C: MSB 0x19", r["read"][0], 0x19, absolute=0)
    sim.check("DS1621 25 C: LSB 0x00", r["read"][1], 0x00, absolute=0)


if __name__ == "__main__":
    run_test(test)
