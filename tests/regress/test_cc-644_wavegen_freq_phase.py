"""A WaveGen's frequency changed during a run goes on from the same phase (create-circuit #644).

Every setting change restarted the wave at phase 0 (ClockBase::updateStep), so stepping Freq to
emulate FM put a short or long period at each step (one 11.9 us period among 9.5 us ones).
Worse, the restart forced the pin low while the wave kept its last value, so the output stayed
low until the next edge it computed.
Expected by hand, a Square 1 kHz (0 / 1 V) into 1 k:
- at 0.3 ms it is high (phase 0.3);
- Freq 2 kHz set there: phase 0.3 is kept, so it stays high to phase 0.5, 0.1 ms later:
  high at 0.35 ms, low at 0.45 ms (from 0.4 ms), high again at 0.7 ms (from 0.65 ms);
- a fresh run still starts at phase 0: high at 0.1 ms.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "0,0", "Wave_Type=Square", "Freq=1kHz",
      "Mid_Volt=0.5", "Semi_Ampli=0.5", "Always_On=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,0", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,40")
    c("wire", "add", "W.outnod", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")
    v = lambda: c("probe", "R.lPin")[0]["voltage"]
    c("sim", "run", "--for", "0.3ms")
    sim.check("high at 0.3 ms", v(), 1.0, absolute=0.01)
    c("comp", "set", "W", "Freq=2kHz")
    c("sim", "run", "--for", "0.05ms")
    sim.check("2 kHz from phase 0.3: high at 0.35 ms", v(), 1.0, absolute=0.01)
    c("sim", "run", "--for", "0.1ms")
    sim.check("low at 0.45 ms", v(), 0.0, absolute=0.01)
    c("sim", "run", "--for", "0.25ms")
    sim.check("high at 0.7 ms", v(), 1.0, absolute=0.01)
    c("sim", "stop")
    c("sim", "run", "--for", "0.1ms")
    sim.check("fresh run at 2 kHz: high at 0.1 ms", v(), 1.0, absolute=0.01)


run_test(test)
