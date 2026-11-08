"""BH-020: setting the DS18B20 ROM property to fewer than 8 hex bytes crashes SimulIDE.

Ds18b20::setROM reads 8 tokens with QStringList::at() without checking the count.
Right result: a short/malformed ROM does not crash; a valid 8-byte ROM is kept.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def rom(sim):
    return {x["name"]: x["value"] for x in sim.cli("comp", "get", "D")["properties"]}["ROM"]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "DS18B20", "--label", "D", "--at", "0,0")
    sim.cli("comp", "set", "D", "ROM=28 01 02 03 04 05 06 07")
    sim.check("valid ROM kept", rom(sim).split() == ["28", "01", "02", "03", "04", "05", "06", "07"], True, tol=0)
    sim.cli("comp", "set", "D", "ROM=abc")
    sim.check("server alive after short ROM", len(rom(sim).split()), 8, tol=0)


run_test(test)
