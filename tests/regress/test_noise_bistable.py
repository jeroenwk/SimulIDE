"""Transient noise decides a symmetric bistable (docs/NOISE_SPEC.md §1, §8 step 5).

Two identical NPNs cross-coupled through 10 kΩ, 1 kΩ collector resistors, 9 V,
10 nF from each base to ground (the time constant of the loop). Perfectly symmetric:
a real one ends in one of its two states at random, decided by noise.

- Noise off: the result is the simulator's own (the solver's order), the same at
  every start.
- Noise on (start-up only, 0–1 ms, physical level ×1): the state depends on the
  seed; over seeds 1..8 both states occur, and each seed always gives the same one.
- Each end state is a proper one: one collector near 0 V, the other high.

Second case, no base capacitors at all (purely resistive): noise off, it stays at its
balance point (both collectors 1.40 V), as before junction capacitance existed. Noise
on: the BJTs get their typical junction capacitance (Cje 4.5 pF, Cjc 3.6 pF) and every
capacitance integrates with the noise step in the window, so the loop has real dynamics
and the seed decides; afterwards, at the normal reactStep, the state holds.
"""

from harness import run_test


def build(sim, base_caps=True):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-120,0", "Voltage=9")
    c("comp", "add", "Ground", "--label", "G", "--at", "-120,120")
    for n, x in (("1", 0), ("2", 240)):
        c("comp", "add", "BJT", "--label", "Q" + n, "--at", "%d,80" % x)
        c("comp", "add", "Resistor", "--label", "RC" + n, "--at", "%d,-40" % x, "Resistance=1000")
        c("comp", "add", "Resistor", "--label", "RX" + n, "--at", "%d,40" % (x + 80), "Resistance=10000")
        if base_caps:
            c("comp", "add", "Capacitor", "--label", "CB" + n, "--at", "%d,140" % (x - 60), "Capacitance=10nF")
    c("wire", "add", "B1.lPin", "RC1.lPin")
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "RC1.lPin", "RC2.lPin")
    for n, o in (("1", "2"), ("2", "1")):
        c("wire", "add", "RC" + n + ".rPin", "Q" + n + ".collector")
        c("wire", "add", "Q" + n + ".emiter", "G.Gnd")
        c("wire", "add", "Q" + n + ".collector", "RX" + n + ".lPin")
        c("wire", "add", "RX" + n + ".rPin", "Q" + o + ".base")
        if base_caps:
            c("wire", "add", "CB" + n + ".lPin", "Q" + n + ".base")
            c("wire", "add", "CB" + n + ".rPin", "G.Gnd")
    c("settings", "set", "reactStep=50ns", "noiseBW=10MHz", "noiseStop=1ms")


def end_state(sim):
    sim.cli("sim", "run", "--for", "2ms")
    v1 = sim.cli("probe", "Q1.collector")[0]["voltage"]
    v2 = sim.cli("probe", "Q2.collector")[0]["voltage"]
    sim.cli("sim", "stop")
    return v1, v2


def test(sim):
    build(sim)
    sim.cli("settings", "set", "noise=off")
    a = end_state(sim)
    b = end_state(sim)
    sim.check("noise off: the same end state twice", 1 if a == b else 0, 1, absolute=0)

    sim.cli("settings", "set", "noise=on")
    states = {}
    for seed in range(1, 9):
        sim.cli("settings", "set", "noiseSeed=%d" % seed)
        v1, v2 = end_state(sim)
        sim.check("seed %d: one collector low, one high" % seed,
                  1 if min(v1, v2) < 0.5 and max(v1, v2) > 7 else 0, 1, absolute=0)
        states[seed] = "Q1 on" if v1 < v2 else "Q2 on"
    sim.log("end states: %s" % states)
    sim.check("noise on: both states occur over seeds 1..8", len(set(states.values())), 2, absolute=0)
    sim.cli("settings", "set", "noiseSeed=1")
    v1, v2 = end_state(sim)
    sim.check("seed 1 again: same state", 1 if ("Q1 on" if v1 < v2 else "Q2 on") == states[1] else 0, 1, absolute=0)

    # Purely resistive: only junction capacitance (with noise) gives it dynamics
    build(sim, base_caps=False)
    sim.cli("settings", "set", "noise=off")
    v1, v2 = end_state(sim)
    sim.check("resistive, noise off: Q1 stays balanced (V)", v1, 1.40, absolute=0.05)
    sim.check("resistive, noise off: Q2 stays balanced (V)", v2, 1.40, absolute=0.05)
    sim.cli("settings", "set", "noise=on")
    states = {}
    for seed in range(1, 7):
        sim.cli("settings", "set", "noiseSeed=%d" % seed)
        v1, v2 = end_state(sim)
        sim.check("resistive, seed %d: one collector low, one high" % seed,
                  1 if min(v1, v2) < 0.5 and max(v1, v2) > 7 else 0, 1, absolute=0)
        states[seed] = "Q1 on" if v1 < v2 else "Q2 on"
    sim.log("resistive end states: %s" % states)
    sim.check("resistive, noise on: both states occur over seeds 1..6", len(set(states.values())), 2, absolute=0)


if __name__ == "__main__":
    run_test(test)
