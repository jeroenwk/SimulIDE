"""Logic gates with an RC loop: a NAND astable froze (create-circuit #213, #206).

Duncan fig 75.4: two NANDs as inverters (both inputs tied; here two inverted Buffers, the same
Gate code), C from A.out to B.in, R from
B.in to B.out, B.out to A.in. When B.in decayed through the 2.5 V threshold, B's output
rose and pushed B.in back up by 13 mV through R (A's 40 ohm output and the capacitor):
B flipped back after one gate delay while A flipped on the real edge, and the pulse
circulated A -> B -> A at the 10 ns gate delay forever, so every sample a run took
showed the same frozen levels. The Scherz 12.79D NAND-latch clock did the same.

Real gates have an inertial delay: an input change undone before the output follows it
is dropped. And a gate barely past its threshold answers after one driven well past it.

Expected (CMOS inverter astable, thresholds at Vdd/2): T = 2 R C ln 3 = 2.2 R C;
R 15 k, C 1 uF: 33.0 ms, 30.3 Hz (10 %).
Inertial delay: a Buffer with a 100 ns delay drops 30 ns input pulses (1 MHz, duty 3 %),
and passes 300 ns ones (duty 30 %): 0 Hz and 1 MHz at its output.
"""

from harness import run_test


def astable(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Buffer", "--label", "A", "--at", "-64,24", "Inverted=true")
    c("comp", "add", "Buffer", "--label", "B", "--at", "64,16", "Inverted=true", "initHigh=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "56,-48", "Resistance=15000")
    c("comp", "add", "Capacitor", "--label", "C", "--at", "0,80", "Capacitance=1 µF")
    c("comp", "add", "Frequency Meter", "--label", "F", "--at", "-64,-80")
    c("wire", "add", "A.out", "C.lPin")
    c("wire", "add", "C.rPin", "B.in0")
    c("wire", "add", "R.lPin", "C.rPin")
    c("wire", "add", "R.rPin", "B.out")
    c("wire", "add", "B.out", "A.in0")
    c("wire", "add", "F.lPin", "A.out")
    c("sim", "run", "--for", "400ms", timeout=300)
    f = c("read", "F").get("value")
    c("sim", "stop")
    return float(str(f).split()[0]) if f is not None else 0.0


def pulses(sim, duty):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-80,0", "Always_On=true", "Wave_Type=Square",
      "Freq=1 MHz", "Duty=%g" % duty, "Mid_Volt=2.5 V")
    c("comp", "add", "Buffer", "--label", "U", "--at", "0,0", "Tpd_ps=100 ns")
    c("comp", "add", "Frequency Meter", "--label", "F", "--at", "80,0")
    c("wire", "add", "W.outnod", "U.in0")
    c("wire", "add", "U.out", "F.lPin")
    c("sim", "run", "--for", "2ms", timeout=300)
    f = c("read", "F").get("value")
    c("sim", "stop")
    return float(str(f).split()[0]) if f is not None else 0.0


def test(sim):
    sim.check("NAND RC astable (Hz)", astable(sim), 1 / (2.197 * 15e3 * 1e-6), tol=0.10)
    sim.check("30 ns pulses through a 100 ns gate (Hz)", pulses(sim, 3), 0, absolute=1)
    sim.check("300 ns pulses through a 100 ns gate (Hz)", pulses(sim, 30), 1e6, tol=0.01)


run_test(test)
