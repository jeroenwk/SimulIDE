"""BH-043: DynamicMemory builds the cell address as row * 2^Row_Bits + column.

DynamicMemory::voltChanged (logic/dynamic_memory.cpp) computes `m_rowAddress * pow(2, m_rowAddrBits)`,
but the column field is Column_Bits wide: the address must be row * 2^Column_Bits + column.
With more row than column bits the address runs past the end of the m_ram vector (size
2^(rows+cols)): Row_Bits=8, Column_Bits=4, row 255 / column 15 writes element 65295 of a
4096 element vector (heap overwrite). With fewer row bits than column bits different cells overlap:
Row_Bits=4, Column_Bits=8: (row 0, col 16) and (row 1, col 0) are the same cell.
Right result: every (row, column) pair is its own cell, the write goes to index row*2^cols+col.
This test: Row_Bits=2, Column_Bits=4 (A0..A3 shared), 64 cells; write 0xAA to (row 0, col 4) and 0x55
to (row 1, col 0): the first must not be overwritten (both land on 4 in the buggy code), read it back.
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test

ADDR = 4
DATA = 8


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Dynamic Ram", "--label", "M", "--at", "0,0",
            "Row_Bits=2", "Column_Bits=4", "Data_Bits=8")
    names = {}
    pins = ["A%d" % i for i in range(ADDR)] + ["D%d" % i for i in range(DATA)] + ["WE", "RAS", "CAS", "OE"]
    for i, p in enumerate(pins):
        v = "V" + p
        sim.cli("comp", "add", "Fixed Voltage", "--label", v, "--at", "%d,%d" % (-300 - 40 * (i % 3), 40 * i),
                "Voltage=0", "Out=true")
        if p.startswith("D"):      # weak drive, so the memory's output buffers win on a read
            sim.cli("comp", "add", "Resistor", "--label", "R" + p, "--at", "%d,%d" % (-200, 40 * i), "Resistance=100k")
            sim.cli("wire", "add", v + ".outnod", "R%s.lPin" % p)
            sim.cli("wire", "add", "R%s.rPin" % p, "M." + p)
        else:
            sim.cli("wire", "add", v + ".outnod", "M." + p)
    sim.cli("sim", "run", "--for", "1ms")

    def setv(p, hi): sim.cli("comp", "set", "V" + p, "Voltage=%d" % (5 if hi else 0))
    def step(): sim.cli("sim", "run", "--for", "100us")

    def access(row, col, write, data=0):
        # pins: WE / RAS / CAS are active low
        for p in ("RAS", "CAS", "OE"): setv(p, True)
        setv("WE", not write)
        step()
        for b in range(ADDR): setv("A%d" % b, (row >> b) & 1)
        if write:
            for b in range(DATA): setv("D%d" % b, (data >> b) & 1)
        step()
        setv("RAS", False); step()
        for b in range(ADDR): setv("A%d" % b, (col >> b) & 1)
        step()
        setv("CAS", False); step()
        if not write:
            setv("OE", False); step()
            pv = sim.read("M")["pins"]
            v = 0
            for b in range(DATA):
                v |= (1 if pv.get("out%d" % b, 0) > 2.5 else 0) << b
            return v

    access(0, 4, True, 0xAA)
    access(1, 0, True, 0x55)
    for p in ("RAS", "CAS", "OE", "WE"): setv(p, True)
    step()
    got = access(0, 4, False)
    sim.check("cell (row 0, col 4) keeps 0xAA after writing (row 1, col 0)", got, 0xAA, tol=0)


run_test(test)
