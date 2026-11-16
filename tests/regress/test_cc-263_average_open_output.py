"""sim average of a logic output with no wire (create-circuit #263).

An unused output (a flip-flop's Q-bar) has no circuit node, and sim average refused it
with "U1.out is not connected", although the pin drives its own level. It is now read at
every solved step: an inverter driven by a 1 kHz clock averages 2.5 V (0..5 V), as the
clock itself does. A pin of a passive part with no node is still refused.
"""

from harness import run_test
from simctl import SimError


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Clock", "--label", "K1", "--at", "-80,0", "Always_On=true")
    sim.cli("comp", "add", "Buffer", "--label", "U1", "--at", "0,0", "Inverted=true")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "0,80")
    sim.cli("wire", "add", "K1.outnod", "U1.in0")
    av = sim.cli("sim", "average", "--for", "10ms", "U1.out", "K1.outnod", timeout=60)
    ch = {c["ref"]: c["voltage"] for c in av["channels"]}
    sim.check("open output mean", ch["U1.out"]["mean"], 2.5, tol=0.02)
    sim.check("open output max", ch["U1.out"]["max"], 5.0, tol=0.01)
    sim.check("open output min", ch["U1.out"]["min"], 0.0, absolute=0.01)
    sim.check("clock mean", ch["K1.outnod"]["mean"], 2.5, tol=0.02)

    sim.cli("sim", "stop")
    refused = False
    try:
        sim.cli("sim", "average", "--for", "1ms", "R1.lPin", timeout=60)
    except SimError as e:
        refused = "not connected" in str(e)
    sim.check("open resistor pin still refused", 1 if refused else 0, 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
