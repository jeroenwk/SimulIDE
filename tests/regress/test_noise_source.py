"""Noise source part (NOISE_SPEC.md §11; AoE ch. 8 and Figs. 4.31-4.33: a comparator on a noisy input).

A floating voltage source (+, -, through Out_Imped) whose voltage is white + 1/f + random
telegraph + hum + offset. Expected values from the definitions:
- White 1 V rms, 10 kHz: the samples (a new Gaussian value every 50 us) have rms 1 V and mean
  0: 600 samples, rms within 10 %, mean within 0.15 V (3 sigma / sqrt(600) = 0.12).
- 1/f, density 10 mV/sqrt(Hz) at 1 Hz, from 10 Hz to 1 kHz: variance = K ln(fmax/fmin)
  = 1e-4 * ln(100) = 4.6e-4, rms 21.5 mV. Samples 20 ms apart (the slowest process's
  correlation time is 16 ms), 400 of them: within 25 % (the sum of first-order processes
  approximates K/f within a few %; the rest is the spread of 400 correlated samples).
- Telegraph 1 V, mean 1 ms high and 3 ms low: high 25 % of the time: 500 samples 2 ms
  apart, within 0.25 +- 0.08.
- Hum 2 V peak at 50 Hz: the largest of samples over 3 periods is 2 V within 2 %.
- Offset 0.5 V with nothing else: 0.5 V exactly.
- Seed 7: the same value at the same time in two runs; it differs from Seed 8.
- Into a 1 Ohm Out_Imped and a 1 Ohm load: half the voltage.
"""

import math
from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def source(sim, *props, load=1e6):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "NoiseSource", "--label", "N", "--at", "0,0", "White=0", *props)
    c("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "60,0", "--rot", "90", "Resistance=%g" % load)
    c("wire", "add", "N.minus", "G.Gnd")
    c("wire", "add", "N.plus", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")


def samples(sim, n, dt):
    out = []
    for i in range(n):
        sim.cli("sim", "run", "--for", dt)
        out.append(sim.cli("probe", "N.plus")[0]["voltage"])
    sim.cli("sim", "stop")
    return out


def rms(v):
    return math.sqrt(sum(x * x for x in v) / len(v))


def test(sim):
    c = sim.cli
    try:
        source(sim, "White=1", "Bandwidth=10 kHz")
    except Exception as e:
        sim.check("Noise source part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    v = samples(sim, 600, "137us")
    within(sim, "white 1 V rms: rms (V)", rms(v), 0.9, 1.1)
    within(sim, "white 1 V rms: mean (V)", sum(v) / len(v), -0.15, 0.15)

    source(sim, "Flicker=10 mV", "Fmin=10", "Bandwidth=1 kHz")
    c("sim", "run", "--for", "100ms")
    v = samples(sim, 400, "20ms")
    want = math.sqrt(1e-4 * math.log(100))
    within(sim, "1/f 10 mV/rtHz, 10 Hz-1 kHz: rms (mV), %.1f by hand" % (want * 1e3), rms(v) * 1e3,
           want * 1e3 * 0.75, want * 1e3 * 1.25)

    source(sim, "RTS_Amp=1", "RTS_High=1 ms", "RTS_Low=3 ms", "Bandwidth=100 kHz")
    v = samples(sim, 500, "2ms")
    within(sim, "telegraph 1 ms / 3 ms: fraction high", sum(1 for x in v if x > 0.5) / len(v), 0.17, 0.33)

    source(sim, "Hum=2", "Hum_Freq=50", "Bandwidth=100 kHz")
    v = samples(sim, 300, "0.2ms")
    within(sim, "hum 2 V peak (V)", max(v), 1.96, 2.0)

    source(sim, "Offset=0.5")
    within(sim, "offset 0.5 V (V)", samples(sim, 1, "1ms")[0], 0.4999, 0.5001)

    source(sim, "Offset=1", load=1)
    within(sim, "1 V into 1 ohm through 1 ohm (V)", samples(sim, 1, "1ms")[0], 0.499, 0.501)

    got = []
    for seed in (7, 7, 8):
        source(sim, "White=1", "Seed=%d" % seed)
        got.append(samples(sim, 1, "1.234ms")[0])
    sim.check("seed 7 repeats (%.4f, %.4f)" % (got[0], got[1]), int(got[0] == got[1]), 1, absolute=0)
    sim.check("seed 8 differs (%.4f)" % got[2], int(got[2] != got[0]), 1, absolute=0)


run_test(test)
