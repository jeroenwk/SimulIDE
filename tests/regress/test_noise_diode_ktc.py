"""Transient noise (docs/NOISE_SPEC.md §3.2): shot noise of a forward-biased diode.

The shot noise 2q·I through the diode's own small-signal resistance r_d = n·Vt/I,
with a capacitor C across the diode, gives v² = 2qI·r_d/(4C) = n·kT/(2C), the same
for any bias current. The bias resistor is left out of the noise (noiseOff), so only
the diode counts; r_d ≪ R_bias, so R_bias does not change the bandwidth.

- I ≈ 100 µA (1.6 V through 10 kΩ), C = 1 nF: r_d·C ≈ 0.5 µs.
- I ≈ 1 mA (1.6 V through 1 kΩ), C = 10 nF: r_d·C ≈ 0.5 µs.
- n = the diode's EmCoef (read from the part). Noise bandwidth 100 MHz, reactStep 5 ns.
- 15 % tolerance: the diode is linearised at its operating point.
"""

import math
from harness import run_test

KT = 1.380649e-23 * 300.15


def case(sim, rbias, cap, capf):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=1.6")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    c("comp", "add", "Resistor", "--label", "RB", "--at", "0,-40", "Resistance=%d" % rbias)
    c("comp", "add", "Diode", "--label", "D1", "--at", "80,40", "--rot", "90")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "160,40", "--rot", "90", "Capacitance=" + cap)
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "RB.lPin")
    c("wire", "add", "RB.rPin", "D1.lPin")
    c("wire", "add", "D1.lPin", "C1.lPin")
    c("wire", "add", "D1.rPin", "G.Gnd")
    c("wire", "add", "C1.rPin", "G.Gnd")
    props = c("comp", "get", "D1")["properties"]
    n = None
    for p in props:
        if p.get("name") == "EmCoef":
            n = float(str(p["value"]).split()[0])
    c("settings", "set", "reactStep=5ns", "noise=on", "noiseBW=100MHz", "noiseSeed=5", "noiseOff=RB")
    c("sim", "run", "--for", "20us")
    i = abs(c("probe", "D1.lPin")[0]["current"])
    r = c("sim", "average", "--for", "2ms", "D1.lPin", timeout=600)
    v = r["channels"][0]["voltage"]
    sd = math.sqrt(max(v["rms"] ** 2 - v["mean"] ** 2, 0))
    want = math.sqrt(n * KT / (2 * capf))
    sim.check("I = %.3g A, C = %s: rms = sqrt(n·kT/2C), n = %g" % (i, cap, n), sd, want, tol=0.15)
    c("sim", "stop")


def test(sim):
    case(sim, 10000, "1nF", 1e-9)
    case(sim, 1000, "10nF", 10e-9)


if __name__ == "__main__":
    run_test(test)
