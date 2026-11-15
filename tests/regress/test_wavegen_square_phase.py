"""WaveGen Phase on a Square wave (create-circuit #193, #210).

Phase shifted Sine, Saw and Triangle, but a Square kept its edges at the unshifted
times: genSquare() toggled from edge to edge and never read the phase-shifted time.

1 kHz square, 50 % duty, sampled at 0.125 ms steps:
- Phase 0: high from 0 to 0.5 ms (0.125, 0.375 high; 0.625, 0.875 low);
- Phase 90: delayed by a quarter period, as the sine is: high from 0.25 to 0.75 ms
  (0.125 low, 0.375 and 0.625 high, 0.875 low);
- Phase 90 with Duty 25: high from 0.25 to 0.5 ms (0.375 high, 0.625 low).
"""

from harness import run_test


def samples(sim, phase, duty=50):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "0,0", "Always_On=true", "Wave_Type=Square",
      "Phase=%g" % phase, "Duty=%g" % duty)
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,0")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,80")
    c("wire", "add", "W.outnod", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")
    c("sim", "start")
    vs = []
    for _ in range(8):
        c("sim", "run", "--for", "0.125ms")
        vs.append(1 if c("probe", "W.outnod")[0]["voltage"] > 0 else 0)
    c("sim", "stop")
    return vs[0::2]


def test(sim):
    for phase, duty, want in ((0, 50, [1, 1, 0, 0]), (90, 50, [0, 1, 1, 0]), (90, 25, [0, 1, 0, 0])):
        got = samples(sim, phase, duty)
        sim.check("Phase %d Duty %d: levels at 0.125/0.375/0.625/0.875 ms %s (want %s)" % (phase, duty, got, want),
                  int(got == want), 1, absolute=0)


run_test(test)
