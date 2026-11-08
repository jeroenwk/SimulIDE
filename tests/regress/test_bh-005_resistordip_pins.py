"""BH-005: ResistorDip declares its own m_pin, hiding Component::m_pin, so getPins() is empty.

A ResistorDip of Size n has 2*n pins. The CLI (and move-undo, theme, remove code) use
Component::getPins(), so with the bug the part reports 0 pins and cannot be wired.
Check: pin count = 2*Size; a divider 10 V - dip resistor (1 kohm) - 1 kohm to ground gives 5 V.
"""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "ResistorDip", "--label", "D1", "--at", "0,0", "Resistance=1000", "Size=3")
    pins = sim.cli("comp", "get", "D1")["pins"]
    sim.check("pin count (2*Size)", len(pins), 6, tol=0.01)
    if not pins:
        return
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "-100,0", "Voltage=10")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,0", "Resistance=1000")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,100")
    sim.cli("wire", "add", "B1.lPin", pins[0]["ref"])
    sim.cli("wire", "add", pins[1]["ref"], "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    sim.run("2ms")
    sim.check("divider midpoint (V)", sim.read("R1")["pins"]["lPin"], 5.0, tol=0.01)


if __name__ == "__main__":
    run_test(test)
