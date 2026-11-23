"""MF5 switched-capacitor filter (Scherz Fig. 9.24), against the MF10 datasheet's mode equations.

TI MF10-N (SNOS547C, 2013; the MF5 is one MF10 section), modes p. 15-18, +-5 V supplies,
AGND at ground, sine 0.1 V peak in, clock a 100 kHz square, 50/100 pin at AGND (100:1):
Mode 1 (R1 = R2 = 10 k, R3 = 100 k, SA to V+): f0 = fCLK/100 = 1 kHz, Q = R3/R2 = 10,
HOBP = -R3/R1 = -10, HOLP = -R2/R1 = -1, notch at f0 on N:
- at 1 kHz: BP 1.0 V, LP = Q |HOLP| Vin = 1.0 V (within 5 %), N < 20 mV (notch);
- at 100 Hz: LP = 0.1 / (1 - 0.01) = 0.101 V, BP = 0.010 V (within 5 %);
- at 950 Hz: BP = 1 / sqrt(1 + Q^2 (0.95 - 1/0.95)^2) = 0.698 V (within 5 %).
- Clock 50 kHz: f0 500 Hz: BP at 500 Hz 1.0 V. 50/100 pin at V+: 50:1, f0 2 kHz: BP 1.0 V.
- 50/100 pin at V-: filtering stops (outputs held).
Mode 4 (R1 = R2 = 10 k, R3 = 100 k, S1 = Vin, SA to V+): HOLP = -(1 + R2/R1) = -2:
LP at 100 Hz 0.202 V; N is an allpass: 0.1 V at 300 Hz, 1 kHz and 3 kHz (within 0.5 dB:
the datasheet notes 0.4 dB of peaking near f0 from the sampling).
Mode 3 (state variable: R1 = R2 = R3 = R4 = 10 k, R2 N->INV, R3 BP->INV, R4 LP->INV, SA to V-
so the second summer input is AGND; Scherz Fig. 9.24): f0 = fCLK/100 sqrt(R2/R4) = 1 kHz,
Q = sqrt(R2/R4) R3/R2 = 1, HOLP = -R4/R1, HOBP = -R3/R1, HOHP = -R2/R1 = -1:
LP at 100 Hz 0.1005 V, BP at 1 kHz 0.1 V, HP at 10 kHz 0.1005 V (within 5 %).
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def amp(sim, pin, f, n=40):
    lo, hi = 99, -99
    for i in range(n):
        sim.cli("sim", "run", "--for", "%gus" % (1e6 / f / n * 1.37))
        v = sim.cli("probe", pin)[0]["voltage"]
        lo, hi = min(lo, v), max(hi, v)
    return (hi - lo) / 2


def filt(sim, fin, fclk=100e3, ratio="G.Gnd", mode=1):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "MF5", "--label", "U", "--at", "0,0")
    c("comp", "add", "Fixed Voltage", "--label", "VP", "--at", "160,-120", "Voltage=5", "Out=true")
    c("comp", "add", "Fixed Voltage", "--label", "VN", "--at", "160,120", "Voltage=-5", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "100,160")
    c("comp", "add", "WaveGen", "--label", "CK", "--at", "-200,80", "Wave_Type=Square", "Freq=%g Hz" % fclk,
      "Mid_Volt=0", "Semi_Ampli=5", "Always_On=true")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,-120", "Wave_Type=Sine", "Freq=%g Hz" % fin,
      "Mid_Volt=0", "Semi_Ampli=0.1", "Always_On=true")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-120,-40", "Resistance=10000")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "0,-80", "Resistance=10000")
    c("comp", "add", "Resistor", "--label", "R3", "--at", "0,-120", "Resistance=100000")
    c("comp", "add", "Resistor", "--label", "RA", "--at", "200,40", "--rot", "90", "Resistance=10000")
    for a, b in [("VP.outnod", "U.vp"), ("VN.outnod", "U.vn"), ("U.agnd", "G.Gnd"), ("U.lsh", "G.Gnd"),
                 ("U.ratio", ratio), ("VN.outnod" if mode == 3 else "VP.outnod", "U.sa"), ("CK.outnod", "U.clk"), ("W.outnod", "R1.lPin"),
                 ("R1.rPin", "U.inv"), ("U.inv", "R2.lPin"), ("R2.rPin", "U.n"), ("U.inv", "R3.lPin"),
                 ("R3.rPin", "U.bp")]:
        c("wire", "add", a, b)
    c("wire", "add", "U.s1", "W.outnod" if mode == 4 else "G.Gnd")
    if mode != 3:
        c("wire", "add", "U.lp", "RA.lPin")
        c("wire", "add", "RA.rPin", "G.Gnd")
    if mode == 3:
        c("comp", "set", "R3", "Resistance=10000")
        c("comp", "add", "Resistor", "--label", "R4", "--at", "0,-160", "Resistance=10000")
        c("wire", "add", "U.inv", "R4.lPin")
        c("wire", "add", "R4.rPin", "U.lp")
    c("sim", "run", "--for", "30ms", timeout=900)


def test(sim):
    c = sim.cli
    try:
        filt(sim, 1000)
    except Exception as e:
        sim.check("MF5 part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    within(sim, "mode 1, 1 kHz: BP (V)", amp(sim, "U.bp", 1000), 0.95, 1.05)
    within(sim, "mode 1, 1 kHz: LP (V)", amp(sim, "U.lp", 1000), 0.95, 1.05)
    within(sim, "mode 1, 1 kHz: notch N (V)", amp(sim, "U.n", 1000), 0, 0.02)
    c("sim", "stop")
    filt(sim, 100)
    within(sim, "mode 1, 100 Hz: LP (V)", amp(sim, "U.lp", 100), 0.096, 0.106)
    within(sim, "mode 1, 100 Hz: BP (V)", amp(sim, "U.bp", 100), 0.0096, 0.0106)
    c("sim", "stop")
    filt(sim, 950)
    within(sim, "mode 1, 950 Hz: BP (V), 0.698 by hand", amp(sim, "U.bp", 950), 0.663, 0.733)
    c("sim", "stop")
    filt(sim, 500, fclk=50e3)
    within(sim, "clock 50 kHz: BP at 500 Hz (V)", amp(sim, "U.bp", 500), 0.95, 1.05)
    c("sim", "stop")
    filt(sim, 2000, ratio="VP.outnod")
    within(sim, "50/100 at V+ (50:1): BP at 2 kHz (V)", amp(sim, "U.bp", 2000), 0.95, 1.05)
    c("sim", "stop")
    filt(sim, 1000, ratio="VN.outnod")
    within(sim, "50/100 at V-: filtering stopped, BP (V)", amp(sim, "U.bp", 1000), 0, 0.001)
    c("sim", "stop")

    filt(sim, 100, mode=4)
    within(sim, "mode 4, 100 Hz: LP (V), HOLP -2", amp(sim, "U.lp", 100), 0.192, 0.212)
    c("sim", "stop")
    for f in (300, 1000, 3000):
        filt(sim, f, mode=4)
        within(sim, "mode 4 allpass, %d Hz: N (V)" % f, amp(sim, "U.n", f), 0.0944, 0.106)
        c("sim", "stop")

    for f, pin, name in ((100, "U.lp", "LP"), (1000, "U.bp", "BP"), (10000, "U.n", "HP")):
        filt(sim, f, mode=3)
        within(sim, "mode 3 (SA at V-), %d Hz: %s (V), 0.1 by hand" % (f, name), amp(sim, pin, f), 0.095, 0.1055)
        c("sim", "stop")


run_test(test)
