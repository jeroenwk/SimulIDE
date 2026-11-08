"""BH-008: LogicComponent::getPins lists the clock pin twice.

FlipFlop D/T/JK/RS, Counter and Shift Reg keep their clock pin in m_inPin and
also set m_clkPin to it; LogicComponent::getPins appended m_clkPin again, so
`comp get` showed the pin twice (and counted one pin too many).

Right result: every pin ref appears exactly once.
"""

from harness import run_test

PARTS = ["FlipFlopD", "FlipFlop T", "FlipFlop JK", "FlipFlop RS"]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    for n, part in enumerate(PARTS):
        sim.cli("comp", "add", part, "--label", "P%d" % n, "--at", "%d,0" % (n * 100))
        refs = [p["ref"] for p in sim.cli("comp", "get", "P%d" % n)["pins"]]
        sim.check("%s: duplicate pin refs" % part, len(refs) - len(set(refs)), 0, tol=0)


if __name__ == "__main__":
    run_test(test)
