"""BH-007: Encoder(10/16 to 4) (DecToBcd) has one input pin that never does anything.

The priority loop in DecToBcd::voltChanged starts at m_bits-2, i.e. at the
input with value m_bits-1 (D9 for 10 bits, D15 for 16 bits), and output = pin
index + 1. But setNumInps(m_bits, "D", 1) creates m_bits pins labelled
D1..D10 (D1..D16), so the last pin (D10 / D16) is ignored: driving it high
alone leaves A..D at 0, as if nothing were pressed. The 74HC147 subcircuit
only uses in0..in8 (9 inputs), which matches the loop.

Right result: every input pin the part shows, driven high alone, changes the
outputs (output = the pin's number, 4-bit). Input pins that cannot encode must
not exist.
"""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Encoder(10/16 to 4)", "--label", "E", "--at", "0,0")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,300")
    for i in range(4):
        sim.cli("comp", "add", "Resistor", "--label", "R%d" % i, "--at", "200,%d" % (i * 20), "Resistance=10000")
        sim.cli("wire", "add", "E.out%d" % i, "R%d.lPin" % i)
        sim.cli("wire", "add", "R%d.rPin" % i, "G.Gnd")
    for bits16 in ("false", "true"):
        sim.cli("comp", "set", "E", "_16_Bits=%s" % bits16)
        pins = [p for p in sim.cli("comp", "get", "E")["pins"] if p["label"].startswith("D") and p["id"].startswith("in")]
        for p in pins:
            n = int(p["label"][1:])
            sim.cli("comp", "add", "Fixed Voltage", "--label", "F", "--at", "-100,0", "Out=true")
            sim.cli("wire", "add", "E." + p["id"], "F.outnod")
            sim.run("2ms")
            val = sum(1 << i for i in range(4) if sim.read("R%d" % i)["pins"].get("lPin", 0) > 2.5)
            sim.check("16bit=%s %s alone -> code" % (bits16, p["label"]), val, n, tol=0)
            sim.cli("comp", "remove", "F")


if __name__ == "__main__":
    run_test(test)
