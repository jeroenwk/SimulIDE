"""BH-003: a default Dial (Min 0, Max 1000, Steps 1000) overshoots its maximum.

The knob ranges 0..1000 but Dial::updateStep maps with (steps-1)=999, so the full-scale
position sends 1001 to the linked part (a Clock here: 1.001 kHz) instead of Max_Val=1000.
Expected: full scale = Max_Val, and the dial's maximum position is Steps-1.
"""

from harness import run_test


def freq_khz(sim):
    props = sim.cli("comp", "get", "K1")["properties"]
    return float([p for p in props if p["name"] == "Freq"][0]["value"].split()[0])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Dial", "--label", "D1", "--at", "0,0")
    sim.cli("comp", "add", "Clock", "--label", "K1", "--at", "100,0")
    sim.cli("comp", "link", "D1", "K1")
    sim.cli("comp", "set", "D1", "Value=100000")  # clamped to the dial maximum = full scale
    sim.run("1ms")
    sim.check("Clock kHz at full scale (Max_Val=1000)", freq_khz(sim), 1.0, tol=0.0005)
    sim.cli("comp", "set", "D1", "Value=0")
    sim.run("1ms")
    sim.check("Clock kHz at zero", freq_khz(sim) + 1.0, 1.0, tol=0.0005)


if __name__ == "__main__":
    run_test(test)
