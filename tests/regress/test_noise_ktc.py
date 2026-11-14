"""Transient noise (docs/NOISE_SPEC.md §3.3): thermal noise of a resistor R with a
capacitor C across it gives v_rms = sqrt(kT/C), whatever R is, as long as the noise
bandwidth is far above 1/(2*pi*R*C).

1 kΩ ∥ 1 nF (RC = 1 µs, corner 159 kHz), noise bandwidth 100 MHz (sources redrawn
every 5 ns; the capacitor follows that step in the noise window, reactStep stays 1 µs): sqrt(1.380649e-23 * 300.15 / 1e-9) = 2.036 µV.
Scale ×100 multiplies it by 100 (noise power by 1e4). The mean stays 0.
The average over 2 ms is 2000 time constants: the rms is within a few % of the
true value; 10 % tolerance.
"""

import math
from harness import run_test

KT = 1.380649e-23 * 300.15


def build(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "0,0", "Resistance=1000")
    sim.cli("comp", "add", "Capacitor", "--label", "C1", "--at", "0,80", "Capacitance=1nF")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "80,160")
    sim.cli("wire", "add", "R1.lPin", "C1.lPin")
    sim.cli("wire", "add", "R1.rPin", "C1.rPin")
    sim.cli("wire", "add", "C1.rPin", "G.Gnd")
    # reactStep left at its 1 µs default: in the noise window the capacitor integrates with
    # the noise step (5 ns) by itself
    sim.cli("settings", "set", "noise=on", "noiseBW=100MHz", "noiseSeed=1")


def rms(sim):
    sim.cli("sim", "stop")
    r = sim.cli("sim", "average", "--for", "2ms", "C1", timeout=600)
    ch = r["channels"][0]["voltage"]
    return ch["rms"], ch["mean"], r


def test(sim):
    build(sim)
    expected = math.sqrt(KT / 1e-9)
    v, mean, r = rms(sim)
    sim.check("reply carries the noise tag", 1 if "noise" in r else 0, 1, absolute=0)
    sim.check("kT/C rms, x1 (V)", v, expected, tol=0.10)
    sim.check("mean (V)", mean, 0, absolute=0.2 * expected)

    sim.cli("settings", "set", "noiseScale=100")
    v, mean, r = rms(sim)
    sim.check("kT/C rms, x100 (V)", v, 100 * expected, tol=0.10)


if __name__ == "__main__":
    run_test(test)
