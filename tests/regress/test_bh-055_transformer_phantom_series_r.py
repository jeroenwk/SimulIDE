"""BH-055: the transformer's coil model adds the same phantom series resistance
as the plain inductor had (BH-053).

A series RLC bandpass in which the inductor is a single transformer winding:
WaveGen -> C 120 nF -> Transformer (Primary=1,1, Secondary=0, BaseInd=50 mH,
i.e. one 50 mH winding) -> R 54 ohm -> ground.

At the resonance f0 = 1/(2*pi*sqrt(L*C)) = 2055 Hz the reactances cancel and
only R is in the loop, so |Vout/Vin| = 1 (Q = 2*pi*f0*L/R = 12, half-power
edges at 1969 and 2141 Hz with |H| = 0.707).

Transformer::runEvent() accumulates into each eCoil's current source a current
computed from the freshly solved coil voltages (transformer.cpp runEvent ->
eCoil::stampCurrent, which does m_currSource += current). The coil's shunt
admittance inva[i][i] (= dt/L, like the plain inductor's) already carries the
current voltage during the interval, so the fresh sample is counted twice
whenever anything else solves between the transformer's updates - here the
WaveGen, every period/Steps. The winding then behaves as a resistor of about
w^2*L*reactStep in series: measured 95 ohm at 10 kHz for a 25 mH winding,
and this bandpass reads 0.865 at f0 instead of 1.0 (Q ~ 10.3).

The plain Inductor had the same bug and was fixed in PR #54 (BH-053); the
transformer is a separate code path (eCoil, not eReactive).
"""

from harness import run_test


def rms(sim, pin, span):
    r = sim.cli("sim", "average", "--for", span, pin)
    return r["channels"][0]["voltage"]["rms"]


def ratio_at(sim, freq, span="20 ms", settle="60 ms"):
    sim.cli("comp", "set", "GEN", "Freq=%s" % freq)
    sim.cli("sim", "run", "--for", settle)
    vi = rms(sim, "GEN.outnod", span)
    vo = rms(sim, "T1.P01Pin", span)
    return vo / vi


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "WaveGen", "--label", "GEN", "--at", "0,0",
            "Wave_Type=Sine", "Freq=2.055 kHz", "Semi_Ampli=1 V", "Mid_Volt=0 V",
            "Steps=1000 _Steps", "Always_On=true")
    sim.cli("comp", "add", "Capacitor", "--label", "C1", "--at", "100,0", "Capacitance=120 nF")
    sim.cli("comp", "add", "Transformer", "--label", "T1", "--at", "200,0",
            "Primary=1,1", "Secondary=0", "BaseInd=50 mH")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "300,0", "--rot", "90",
            "Resistance=54 Ω")
    sim.cli("comp", "add", "Ground", "--label", "G1", "--at", "300,110")
    sim.cli("wire", "add", "GEN.outnod", "C1.lPin")
    sim.cli("wire", "add", "C1.rPin", "T1.P00Pin")
    sim.cli("wire", "add", "T1.P01Pin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "G1.Gnd")

    # at resonance the output should equal the input (only R in the loop)
    h0 = ratio_at(sim, "2.055 kHz")
    sim.check("bandpass gain at f0 = 2055 Hz with a transformer winding as L "
              "(physics: 1.0, only R in the loop)", h0, 1.0, tol=0.03)

    # half-power points
    h1 = ratio_at(sim, "1.969 kHz")
    sim.check("bandpass gain at f0 - BW/2 = 1969 Hz (physics: 0.707)",
              h1, 0.7071, tol=0.03)

    h2 = ratio_at(sim, "2.141 kHz")
    sim.check("bandpass gain at f0 + BW/2 = 2141 Hz (physics: 0.707)",
              h2, 0.7071, tol=0.03)


if __name__ == "__main__":
    run_test(test)
