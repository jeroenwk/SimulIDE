"""Op-amp gain-bandwidth product (GBW): a single pole at GBW / Gain.

The op amp had flat gain at every frequency, so an op-amp crystal oscillator (Scherz 10.22 A)
oscillated at the step rate and no circuit could show a stage's roll-off. New property GBW
(Hz, 0 = off, the default: circuits stay as they were). Expected by hand, Gain 1e5:
- Follower, GBW 1 MHz, input step 0 -> 1 V: closed-loop pole at GBW (the loop gain is
  GBW/f), tau = 1/(2 pi 1 MHz) = 159 ns: out = 1 - e^-1 = 0.632 V at 159 ns, 0.998 V at 1 us.
- Inverting x10 (1 k / 10 k), GBW 1 MHz: -3 dB at GBW/11 = 90.9 kHz: a 0.1 V sine there
  gives 1.0/sqrt(2) = 0.707 V peak; at 1 kHz 1.0 V.
- GBW 0: the follower step is instant (1 V after one step).
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def follower(sim, gbw):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("settings", "set", "reactStep=1ns")
    c("comp", "add", "OpAmp", "--label", "U", "--at", "0,0", "Volt_Pos=15", "Volt_Neg=-15",
      "Gain=100000", "GBW=%g" % gbw)
    c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-200,-80", "Voltage=1", "Out=false")
    c("wire", "add", "VI.outnod", "U.inputNinv")
    c("wire", "add", "U.output", "U.inputInv")
    c("sim", "run", "--for", "2us")
    c("comp", "set", "VI", "Out=true")
    out = []
    for t in ("159ns", "841ns"):
        c("sim", "run", "--for", t)
        out.append(c("probe", "U.output")[0]["voltage"])
    c("sim", "stop")
    return out


def inverting_peak(sim, freq):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("settings", "set", "reactStep=10ns")
    c("comp", "add", "OpAmp", "--label", "U", "--at", "0,0", "Volt_Pos=15", "Volt_Neg=-15",
      "Gain=100000", "GBW=1 MHz")
    c("comp", "add", "Ground", "--label", "G", "--at", "-120,160")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-300,-80", "Wave_Type=Sine", "Freq=%g" % freq,
      "Mid_Volt=0", "Semi_Ampli=0.1", "Always_On=true", "Steps=200")
    c("comp", "add", "Resistor", "--label", "RI", "--at", "-120,80", "Resistance=1000")
    c("comp", "add", "Resistor", "--label", "RF", "--at", "0,-80", "Resistance=10000")
    c("wire", "add", "W.outnod", "RI.lPin")
    c("wire", "add", "RI.rPin", "U.inputInv")
    c("wire", "add", "U.inputInv", "RF.lPin")
    c("wire", "add", "RF.rPin", "U.output")
    c("wire", "add", "U.inputNinv", "G.Gnd")
    c("sim", "run", "--for", "%gs" % (20 / freq))
    hi = 0
    n = 100
    for i in range(n):
        c("sim", "run", "--for", "%gs" % (1 / freq / n))
        hi = max(hi, abs(c("probe", "U.output")[0]["voltage"]))
    c("sim", "stop")
    return hi


def test(sim):
    try:
        v = follower(sim, 1e6)
    except Exception as e:
        sim.check("op amp GBW property (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    within(sim, "GBW 1 MHz follower, 159 ns after a 1 V step (V)", v[0], 0.60, 0.665)
    within(sim, "GBW 1 MHz follower, 1 us after the step (V)", v[1], 0.99, 1.001)
    v = follower(sim, 0)
    within(sim, "GBW 0 follower, 159 ns after the step (V)", v[0], 0.9999, 1.0001)

    within(sim, "GBW 1 MHz, x10 inverting at 1 kHz: peak (V)", inverting_peak(sim, 1e3), 0.98, 1.01)
    within(sim, "GBW 1 MHz, x10 inverting at 90.9 kHz: peak (V)", inverting_peak(sim, 90.9e3), 0.67, 0.74)


run_test(test)
