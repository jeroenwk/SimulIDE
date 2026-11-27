"""An op amp with a slew-rate limit in a diode feedback loop does not limit-cycle (create-circuit
#650, BH-059).

AoE fig 4.36 precision rectifier with Gain 1e5, SlewRate 15 V/us, 10k load, 50 mV 1 kHz sine:
the output hopped in 60 mV steps (the slew step: 0.2 % of the 30 V span) and the rectified
output peaked at 103 mV instead of 50 mV; a following buffer with the same slew rate reached
154 mV. SlewRate 0 or Gain 1000 gave the right result.

Circuit: rails +-15 V; sine 50 mV, 1 kHz into +; output -> diode anode; cathode = OUT, wired
to -, 10k from OUT to ground; a buffer (same op amp) from OUT. Expected over 5 ms after 5 ms:
- OUT peaks at 50 mV (+-3 mV) and stays at 0 V (within 1 mV) on the negative half cycles
  (the 10k pulls it down; no hops);
- the buffer's output peaks at 50 mV too.
"""
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    op = ("Gain=100000", "SlewRate=15", "Volt_Pos=15", "Volt_Neg=-15")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,-8", "Wave_Type=Sine", "Freq=1kHz",
      "Semi_Ampli=50mV", "Mid_Volt=0V", "Always_On=true")
    c("comp", "add", "opAmp", "--label", "U1", "--at", "0,0", *op)
    c("comp", "add", "Diode", "--label", "D", "--at", "80,0")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "160,60", "--rotate", "90", "Resistance=10k")
    c("comp", "add", "opAmp", "--label", "U2", "--at", "240,0", *op)
    c("comp", "add", "Ground", "--label", "G", "--at", "160,140")
    c("wire", "add", "W.outnod", "U1.inputNinv")
    c("wire", "add", "U1.output", "D.lPin")
    c("wire", "add", "D.rPin", "U1.inputInv")
    c("wire", "add", "D.rPin", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    c("wire", "add", "D.rPin", "U2.inputNinv")
    c("wire", "add", "U2.output", "U2.inputInv")
    c("sim", "run", "--for", "5ms")
    a = c("sim", "average", "--for", "5ms", "D.rPin", "U2.output")
    out, buf = [ch["voltage"] for ch in a["channels"]]
    sim.check("OUT peak (V)", out["max"], 0.05, absolute=0.003)
    sim.check("OUT low (V)", out["min"], 0, absolute=0.001)
    sim.check("buffer peak (V)", buf["max"], 0.05, absolute=0.003)
    c("sim", "stop")


run_test(test)
