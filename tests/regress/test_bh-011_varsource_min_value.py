"""BH-011: changing MinValue / MaxValue of a Voltage Source must not change its output.

Voltage Source with Value_Volt=0.7399 V. Raising MaxValue to 7.37 V and MinValue to
0.37 V keeps 0.7399 V inside [min, max], so the value must stay 0.7399 V
(a range change only clamps). With the bug the dial (1000 steps, min not subtracted)
recomputes the output: 0.737 V, then 1.105 V. The value must also survive save + reload.
Expected values: the value that was set (no formula)."""

from harness import run_test


def val(sim):
    g = sim.cli("comp", "get", "V1")
    for p in g["properties"]:
        if p["name"] == "Value_Volt":
            return float(p["value"].split()[0])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Voltage Source", "--label", "V1", "--at", "0,0")
    sim.cli("comp", "set", "V1", "Value_Volt=0.7399")
    sim.cli("comp", "set", "V1", "MaxValue=7.37V")
    sim.check("value after MaxValue change", val(sim), 0.7399, tol=0.001)
    sim.cli("comp", "set", "V1", "MinValue=0.37V")
    sim.check("value after MinValue change", val(sim), 0.7399, tol=0.001)
    sim.cli("circuit", "save", "/tmp/bh011.sim1")
    sim.cli("circuit", "open", "/tmp/bh011.sim1", "--discard")
    sim.check("value after save+reload", val(sim), 0.7399, tol=0.001)


if __name__ == "__main__":
    run_test(test)
