"""BH-032 (guard for the fix): the op-amp test circuits in samples/opamp/ give their textbook values.

Each circuit stresses one part of the op-amp model: three stages in one loop (state-variable
filter), positive feedback coupled to an integrator (function generator), two op amps in one
loop (composite amplifier), positive and negative feedback together (negative impedance
converter, stable and latched), and open-loop comparators (window comparator). Right: the values
below and no "Not converging" fault. Before the fix: the filter had 3948 faults, the function
generator never started and the composite amplifier stuck at -15 V.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test, ROOT

SAMPLES = os.path.join(ROOT, "samples", "opamp")

# file, run before measuring, averaging span, [(ref, quantity, statistic, expected, tolerance)]
CASES = [
    ("opamp_state_variable_filter", "20ms", "5ms", [
        ("U1.output", "voltage", "max", 1.0, 0.03), ("U1.output", "voltage", "min", -1.0, 0.03),
        ("U2.output", "voltage", "max", 1.0, 0.03)]),
    ("opamp_function_generator", "20ms", "10ms", [
        ("U1.output", "voltage", "rms", 12.0, 0.05), ("U2.output", "voltage", "max", 6.0, 0.1),
        ("U2.output", "voltage", "min", -6.0, 0.1)]),
    ("opamp_composite_amplifier", "10ms", "5ms", [
        ("U2.output", "voltage", "max", 5.0, 0.01), ("U2.output", "voltage", "min", -5.0, 0.01),
        ("U1.output", "voltage", "max", 2.5, 0.01)]),
    ("opamp_negative_impedance", "5ms", "2ms", [
        ("U1.inputNinv", "voltage", "mean", 2.0, 0.01), ("U1.output", "voltage", "mean", 4.0, 0.01),
        ("Ra", "current", "mean", -0.002, 2e-5)]),
    ("opamp_window_comparator", "20ms", "20ms", [
        ("U1.output", "voltage", "mean", 5 / 3, 0.05), ("U2.output", "voltage", "mean", 5 / 3, 0.05)]),
]


def test(sim):
    c = sim.cli
    for name, run, span, checks in CASES:
        c("circuit", "open", os.path.join(SAMPLES, name + ".sim1"), "--discard")
        c("sim", "run", "--for", run, timeout=120)
        refs = sorted({r for r, _, _, _, _ in checks})
        av = c("sim", "average", "--for", span, *refs, timeout=120)
        ch = {x["ref"]: x for x in av["channels"]}
        for ref, q, stat, exp, tol in checks:
            sim.check("%s %s %s %s" % (name.replace("opamp_", ""), ref, q, stat), ch[ref][q][stat], exp, absolute=tol)
        n = str(c("log")).count("Not converging")
        sim.check("%s 'Not converging' faults" % name.replace("opamp_", ""), n, 0, absolute=0.5)
        if name == "opamp_negative_impedance":        # Rs 2k > Z: no stable point, latches at a rail
            c("sim", "stop")
            c("ui", "set", "S1", "off")
            c("sim", "run", "--for", "2ms", timeout=120)
            v = c("sim", "average", "--for", "1ms", "U1.output", timeout=120)["channels"][0]["voltage"]["mean"]
            sim.check("negative_impedance S1 open: |out| latched at the 12 V rail", abs(v), 12.0, absolute=0.01)


if __name__ == "__main__":
    run_test(test)
