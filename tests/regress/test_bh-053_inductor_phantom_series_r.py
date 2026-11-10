"""BH-053: the inductor's companion model adds a phantom series resistance.

A series RLC bandpass (Scherz fig 3.91c values): WaveGen -> C 120 nF -> L 50 mH
-> R 54 ohm -> ground. Resonance f0 = 1/(2*pi*sqrt(L*C)) = 2055 Hz, loop
resistance 54 ohm (ideal L and C), Q = 2*pi*f0*L/R = 12, bandwidth 172 Hz.

At f0 the reactances cancel and the only impedance in the loop is R, so the
output (across R) equals the input: |Vout/Vin| = 1. The half-power points are
f0 +/- BW/2 = 1969 / 2141 Hz with |H| = 1/sqrt(2) = 0.707.

SimulIDE models L and C as backward-Euler companion circuits updated every
reactStep (default 1 us). Proper backward Euler at 1 us errs by O((w*dt)^2) in
amplitude: at 2 kHz w*dt = 0.013, the numerical Q is ~1/(w*dt)^2 = 6000, so the
peak should still read ~1.00.

The bug (passive/reactive/inductor.h updtCurr via e-reactive.cpp runEvent):
the accumulating current source adds the *freshly solved* voltage at each
update, so with anything solving between the inductor's updates (here the
WaveGen, every period/Steps) the current sample is counted twice and the
inductor behaves as if it had a series resistor of about w^2*L*dt and 3-12 %
less reactance. Measured on the buggy code: peak 0.865 at f0 (Q ~ 10.3),
0.652/0.658 at 1969/2141 Hz; a 25 mH inductor reads 95 ohm series R at 10 kHz
and 2.2 kohm at 51 kHz. With the inductor accumulating the voltage from the
*previous* update (proper backward Euler) the replica of the scheme gives
1.0000 at f0.

This test drives the circuit through the CLI the way a user would and checks
the RMS ratio with sim average (which integrates every converged solver step).
"""

from harness import run_test

L = 50e-3
C = 120e-9
R = 54.0
F0 = 2055.0          # 1/(2*pi*sqrt(L*C)) = 2054.6 Hz


def rms(sim, pin, span):
    r = sim.cli("sim", "average", "--for", span, pin)
    return r["channels"][0]["voltage"]["rms"]


def ratio_at(sim, freq, span="20 ms", settle="60 ms"):
    sim.cli("comp", "set", "GEN", "Freq=%s" % freq)
    sim.cli("sim", "run", "--for", settle)
    vi = rms(sim, "GEN.outnod", span)
    vo = rms(sim, "L1.rPin", span)
    return vo / vi


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "WaveGen", "--label", "GEN", "--at", "0,0",
            "Wave_Type=Sine", "Freq=2.055 kHz", "Semi_Ampli=1 V", "Mid_Volt=0 V",
            "Steps=1000 _Steps", "Always_On=true")
    sim.cli("comp", "add", "Capacitor", "--label", "C1", "--at", "100,0", "Capacitance=120 nF")
    sim.cli("comp", "add", "Inductor", "--label", "L1", "--at", "180,0", "Inductance=50 mH")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "270,0", "--rotate", "90",
            "Resistance=54 Ω")
    sim.cli("comp", "add", "Ground", "--label", "G1", "--at", "270,110")
    sim.cli("wire", "add", "GEN.outnod", "C1.lPin")
    sim.cli("wire", "add", "C1.rPin", "L1.lPin")
    sim.cli("wire", "add", "L1.rPin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "G1.Gnd")

    # at resonance the output should equal the input (only R in the loop)
    h0 = ratio_at(sim, "2.055 kHz")
    sim.check("bandpass gain at f0 = 2055 Hz (physics: 1.0, only R in the loop)",
              h0, 1.0, tol=0.03)

    # half-power points: 1/sqrt(2), 3 % tolerance for the model
    h1 = ratio_at(sim, "1.969 kHz")
    sim.check("bandpass gain at f0 - BW/2 = 1969 Hz (physics: 0.707)",
              h1, 0.7071, tol=0.03)

    h2 = ratio_at(sim, "2.141 kHz")
    sim.check("bandpass gain at f0 + BW/2 = 2141 Hz (physics: 0.707)",
              h2, 0.7071, tol=0.03)


if __name__ == "__main__":
    run_test(test)
