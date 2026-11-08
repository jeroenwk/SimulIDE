"""BH-001: a value set on an LDR is overwritten by the dial's quantized value.

LDR range 0..1000 lux, the dial has 1000 steps (1 lux). `comp set L1 Lux=0.5` must keep
0.5 lux: R = R1 * lux^-gamma = 127410 * 0.5^-0.8582 = 230.9 kΩ, so 10 V gives 43.3 µA.
Buggy: the dial's valueChanged handler rewrites the value to 0 lux (R = inf, no current).
Also Lux=2.7 must keep 2.7 (R = 127410*2.7^-0.8582 = 54.3 kΩ, 184 µA), buggy gives 2.
"""

from harness import run_test


def current(sim, lux):
    sim.cli("comp", "set", "L1", "Lux=%s" % lux)
    sim.run("5ms")
    return abs(sim.cli("comp", "get", "L1")["pins"][0]["current"])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Ldr", "--label", "L1", "--at", "0,0")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "80,0", "Voltage=10")
    sim.cli("wire", "add", "B1.lPin", "L1.lPin")
    sim.cli("wire", "add", "L1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    for lux in (0.5, 2.7):
        r = 127410 * lux ** -0.8582
        sim.check("LDR current at %s lux" % lux, current(sim, lux), 10.0 / r, tol=0.01)


if __name__ == "__main__":
    run_test(test)
