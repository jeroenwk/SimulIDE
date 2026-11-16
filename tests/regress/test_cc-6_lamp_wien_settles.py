"""A lamp-stabilised Wien oscillator settles to a steady amplitude (create-circuit #6, AoE fig 7.20A).

The lamp re-stamped its resistance only when it had moved by more than 1e-4. At a dim glow
the filament's 2 kHz ripple moves it by about 2.5e-5, so the resistance stood still and then
jumped in 0.02 Ohm steps: a deadband inside the amplitude loop. The envelope kept bouncing at
about 10 Hz between 6.1 and 9.5 V forever, where the same lamp law integrated on its own (ideal
op amp, same filament equations) settles at 7.752 V within about 10 s. The lamp now follows
its filament to 1e-9.

1 kHz Wien (10k, 16 nF), Rf 400, a 28 V / 40 mA lamp with cold ratio 10, noise on, 12 s:
the output peak over eight 25 ms windows varies by less than 1 %, at 7.75 V +- 2 %.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "opAmp", "--label", "U1", "--at", "0,0", "Volt_Pos=15", "Volt_Neg=-15", "Gain=100000")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "120,-80", "Resistance=10k")
    c("comp", "add", "Capacitor", "--label", "CS", "--at", "200,-80", "Capacitance=16n")
    c("comp", "add", "Resistor", "--label", "RP", "--at", "-80,-80", "--rot", "90", "Resistance=10k")
    c("comp", "add", "Capacitor", "--label", "CP", "--at", "-120,-80", "--rot", "90", "Capacitance=16n")
    c("comp", "add", "Resistor", "--label", "RF", "--at", "80,80", "Resistance=400")
    c("comp", "add", "Lamp", "--label", "L1", "--at", "-80,160", "--rot", "90",
      "RatedPower=1.12", "RatedVolt=28", "ColdRatio=10")
    c("comp", "add", "Ground", "--label", "G1", "--at", "-200,0")
    c("comp", "add", "Ground", "--label", "G2", "--at", "-200,200")
    for a, b in (("U1.output", "CS.rPin"), ("CS.lPin", "RS.rPin"), ("RS.lPin", "U1.inputNinv"),
                 ("RP.lPin", "U1.inputNinv"), ("CP.lPin", "RP.lPin"), ("RP.rPin", "G1.Gnd"),
                 ("CP.rPin", "RP.rPin"), ("RF.rPin", "U1.output"), ("RF.lPin", "U1.inputInv"),
                 ("L1.lPin", "U1.inputInv"), ("L1.rPin", "G2.Gnd")):
        c("wire", "add", a, b)
    c("settings", "set", "noise", "true")
    c("sim", "run", "--for", "12s", timeout=600)
    peaks = []
    for _ in range(8):
        av = c("sim", "average", "--for", "25ms", "U1.output", timeout=120)
        peaks.append(av["channels"][0]["voltage"]["max"])
    c("sim", "stop")
    lo, hi = min(peaks), max(peaks)
    sim.check("envelope steady: (max-min)/max over 200 ms (%.2f..%.2f V)" % (lo, hi), (hi - lo) / hi, 0,
              absolute=0.01)
    sim.check("amplitude (V peak)", hi, 7.752, tol=0.02)


run_test(test)
