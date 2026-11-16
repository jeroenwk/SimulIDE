"""Two NANDs whose timing capacitors couple each gate's output into the other's input
(Scherz fig 12.79D) lock into a 50 MHz lockstep (create-circuit #268, #270).

When the two RC nodes cross their thresholds together, both gates switch at the same
instant and a pulse of exactly one gate delay goes round the latch. With equal delays
it never ends: the frequency meter read 50 MHz (1 / (2 x 10 ns)) for the whole run, the
RC period (2.3 ms, 430 Hz; the book gives 437 Hz) never came. A real gate's delay
jitters, so the pulse falls short of one delay and the gate drops it (inertial delay). With noise on, each output
change of a gate now comes up to 2 % of its delay early or late, from the noise seed:
the latch falls out of step and runs at its RC period. Same seed, same run.
"""

from harness import run_test


def build(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "And Gate", "--label", "NA1", "--at", "0,0", "Inverted=true")
    c("comp", "add", "And Gate", "--label", "NA2", "--at", "160,0", "Inverted=true")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,80", "Resistance=1k")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "160,80", "Resistance=1k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "0,-80", "Capacitance=1u")
    c("comp", "add", "Capacitor", "--label", "C2", "--at", "160,-80", "Capacitance=1u")
    c("comp", "add", "FreqMeter", "--label", "FM", "--at", "320,0")
    c("comp", "add", "Resistor", "--label", "RF", "--at", "240,80", "Resistance=10k")
    c("comp", "add", "Capacitor", "--label", "CF", "--at", "240,160", "Capacitance=10n")
    c("comp", "add", "Ground", "--label", "G", "--at", "240,240")
    # A = NA1.in0: R1 to NA1.out, C1 to NA2.out; B = NA2.in1: C2 to NA1.out, R2 to NA2.out
    for a, b in (("NA1.in0", "R1.lPin"), ("R1.rPin", "NA1.out"), ("NA1.in0", "C1.lPin"), ("C1.rPin", "NA2.out"),
                 ("NA2.in1", "R2.lPin"), ("R2.rPin", "NA2.out"), ("NA2.in1", "C2.lPin"), ("C2.rPin", "NA1.out"),
                 ("NA2.out", "NA1.in1"), ("NA1.out", "NA2.in0"),
                 # the meter behind a 100 us low-pass: the few gate-delay edges where the two
                 # equal RC halves cross their thresholds together would fool its peak counter
                 ("NA2.out", "RF.lPin"), ("RF.rPin", "CF.lPin"), ("CF.rPin", "G.Gnd"), ("RF.rPin", "FM.lPin")):
        c("wire", "add", a, b)


def freq(sim):
    r = sim.cli("read", "FM")
    return float(r.get("value", r.get("frequency", 0)))


def test(sim):
    build(sim)
    sim.cli("settings", "set", "noise=on", "noiseStop=1ms", "noiseSeed=1")
    sim.run("50ms")
    f1 = freq(sim)
    sim.check("noise on: the RC period, not the gate delay (Hz)", f1, 430, tol=0.1)

    sim.cli("sim", "stop")
    sim.cli("settings", "set", "noiseSeed=2")
    sim.run("50ms")
    sim.check("another seed: the RC period too (Hz)", freq(sim), 430, tol=0.1)

    sim.cli("sim", "stop")
    sim.cli("settings", "set", "noiseSeed=1")
    sim.run("50ms")
    sim.check("same seed, same run (Hz)", freq(sim), f1, tol=1e-9)


if __name__ == "__main__":
    run_test(test)
