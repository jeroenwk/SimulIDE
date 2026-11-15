"""Logic at the start: settled gates, and random power-up states with noise (create-circuit #87, #88).

A gate computed its output only when an input changed after the start. A Buffer started
high with its input at 0 V stayed high forever (#87). A ring of inverters started with every
stage equal and toggled in lockstep at 1/(2 tpd) instead of a ring's 1/(2 N tpd) (#88).

Now, after the first solve, the gates take their output from their inputs one by one, with
no delay and a solve in between: the logic starts consistent, and in a ring one gate is left
pending, as in a real ring. With noise on, gates and flip-flops start in a random state from
the noise seed (a part in noiseOff keeps its own start state), as at power-up.

Expected:
- Buffer, initHigh, input at 0 V: low at 1 ns (before its 10 ns delay).
- Rings of 3 and 5 inverters, tpd 10 ns: 1/(2 N tpd) = 16.67 MHz and 10 MHz (2 %).
- NAND SR latch with S and R high (hold), noise on: over seeds 1..12 both states appear,
  the same seed gives the same state, and Q and Q' are complementary.
- D flip-flop with no clock, noise on: both Q states appear over the seeds.
- noiseOff with the latch gates' labels: the same latch state for every seed.
"""

from harness import run_test


def ring(sim, n):
    c = sim.cli
    c("circuit", "new", "--discard")
    for i in range(n):
        c("comp", "add", "Buffer", "--label", "U%d" % i, "--at", "%d,0" % (i * 80), "Inverted=true",
          "Tpd_ps=10 ns")
    for i in range(n):
        c("wire", "add", "U%d.out" % i, "U%d.in0" % ((i + 1) % n))
    c("comp", "add", "Frequency Meter", "--label", "F", "--at", "0,-80")
    c("wire", "add", "F.lPin", "U0.out")
    c("sim", "run", "--for", "20us", timeout=120)
    f = c("read", "F").get("value")
    c("sim", "stop")
    return float(str(f).split()[0]) if f is not None else 0.0


def high(sim, pin):
    return int(sim.cli("probe", pin)[0]["voltage"] > 2.5)


def latch(sim, seed, off=""):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-160,0", "Voltage=5", "Out=true")
    c("comp", "add", "And Gate", "--label", "A", "--at", "0,-40", "Inverted=true")
    c("comp", "add", "And Gate", "--label", "B", "--at", "0,40", "Inverted=true")
    c("comp", "add", "FlipFlopD", "--label", "FF", "--at", "120,0")
    c("wire", "add", "V.outnod", "A.in0")
    c("wire", "add", "V.outnod", "B.in1")
    c("wire", "add", "A.out", "B.in0")
    c("wire", "add", "B.out", "A.in1")
    c("comp", "add", "Ground", "--label", "G", "--at", "200,80")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "200,0", "--rot", "90", "Resistance=10000")
    c("wire", "add", "FF.out0", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    c("settings", "set", "noise=true")
    c("settings", "set", "noiseSeed=%d" % seed)
    c("settings", "set", "noiseOff=%s" % off)
    c("sim", "run", "--for", "1us")
    q, qn = high(sim, "A.out"), high(sim, "B.out")
    ff = high(sim, "FF.out0")
    c("sim", "stop")
    c("settings", "set", "noise=false")
    c("settings", "set", "noiseOff=")
    return q, qn, ff


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Buffer", "--label", "U", "--at", "0,0", "initHigh=true", "Tpd_ps=10 ns")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,40")
    c("wire", "add", "G.Gnd", "U.in0")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "80,40", "--rot", "90", "Resistance=10000")
    c("wire", "add", "U.out", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    c("sim", "run", "--for", "1ns")
    sim.check("Buffer initHigh, input 0 V: output at 1 ns", high(sim, "U.out"), 0, absolute=0)
    c("sim", "stop")

    sim.check("ring of 3 inverters (Hz)", ring(sim, 3), 1 / (6 * 10e-9), tol=0.02)
    sim.check("ring of 5 inverters (Hz)", ring(sim, 5), 1 / (10 * 10e-9), tol=0.02)

    states, ffs, same, comp = set(), set(), 1, 1
    for seed in range(1, 13):
        q, qn, ff = latch(sim, seed)
        states.add(q); ffs.add(ff)
        if q == qn: comp = 0
    for seed in (3, 7):
        if latch(sim, seed) != latch(sim, seed): same = 0
    sim.check("NAND latch: both start states over 12 seeds", len(states), 2, absolute=0)
    sim.check("NAND latch: Q and Q' complementary", comp, 1, absolute=0)
    sim.check("D flip-flop: both start states over 12 seeds", len(ffs), 2, absolute=0)
    sim.check("same seed, same start states", same, 1, absolute=0)

    c("circuit", "new", "--discard")                      # noiseOff: the gates keep their own start
    q0 = None
    kept = 1
    for seed in range(1, 9):
        q = latch(sim, seed, "A,B")[0]
        if q0 is None: q0 = q
        kept &= int(q == q0)
    sim.check("noiseOff gates: the same latch state for every seed", kept, 1, absolute=0)


run_test(test)
