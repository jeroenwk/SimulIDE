"""CD4046B PLL and NE566 VCO (AoE 13.85-13.104, Scherz 10.14).

SimulIDE had no PLL or VCO chip. Both are native parts whose timing current flows through
the external resistors and charges the external capacitor (docs/PLL_VCO.md).

CD4046B (TI SCHS043B, data from Harris, p. 3-124..3-128), VDD 10 V:
- VCO: fmax = 1 / (R1 C1) at VCO IN = VDD with R2 open [DOC: 1.2 MHz typ for R1 10 k,
  C1 50 pF (+32 pF internal) at 10 V; the internal 32 pF is left out]; linear in VCO IN
  [DOC linearity 1 %]; R2 adds fmin = 1 / (R2 C1). R1 10 k, C1 1 nF: 100 kHz at 10 V,
  50 kHz at 5 V, 60 kHz at 5 V with R2 = 100 k. Within 2 % (reactStep 10 ns).
- INHIBIT high stops the VCO.
- Source follower: DEM OUT = VCO IN - 1.8 V [DOC offset 1.8 V typ, RS > 10 k].
- Zener: 5.5 V typ at 50 uA [DOC 4.45..6.15 V].
- Signal input self-biased at VDD/2 through 1 MOhm [DOC R14]: an AC-coupled input works
  (the PC1 lock below drives it directly).
- PLL with phase comparator II (lead-lag filter 10 k, 1 k + 100 nF): locks a 30 kHz
  square: VCO at 30 kHz, VCO IN at 3 V (f / fmax * 10 V), phase pulses high (locked).
- PLL with phase comparator I (XOR) and an RC filter: locks too, VCO IN 3 V again.
NE566 (Philips NE/SE566, 15-Apr-1992, p. 400-401), V+ 12 V:
- f0 = 2 (V+ - VC) / (R1 C1 V+): R1 10 k, C1 10 nF, VC 10.5 V -> 2.5 kHz; VC 9 V -> 5 kHz.
  Within 2 %.
- Triangle 2.4 Vpp, square 5.4 Vpp typ [DOC]: within 10 % (10 k loads on the 50 ohm outputs).
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def wires(c, pairs):
    for a, b in pairs:
        c("wire", "add", a, b)


def pll(sim, vin=None, r2=None, inh=False):
    """4046 at VDD 10 V, R1 10 k, C1 1 nF. vin: a fixed VCO IN voltage (else the loop wires it)."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("settings", "set", "reactStep=10ns")
    c("comp", "add", "Pll4046", "--label", "U", "--at", "0,0")
    c("comp", "add", "Fixed Voltage", "--label", "VD", "--at", "120,-80", "Voltage=10", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "120,160")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-100,40", "--rot", "90", "Resistance=10000")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-100,0", "Capacitance=1 nF")
    c("comp", "add", "Frequency Meter", "--label", "F", "--at", "160,0")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "200,80", "--rot", "90", "Resistance=100000")
    c("comp", "add", "Resistor", "--label", "RP", "--at", "240,80", "--rot", "90", "Resistance=100000")
    wires(c, [("VD.outnod", "U.vdd"), ("U.vss", "G.Gnd"), ("U.r1", "R1.lPin"), ("R1.rPin", "G.Gnd"),
              ("C1.lPin", "U.c1a"), ("C1.rPin", "U.c1b"), ("F.lPin", "U.vcoout"),
              ("U.sf", "RS.lPin"), ("RS.rPin", "G.Gnd"), ("U.pp", "RP.lPin"), ("RP.rPin", "G.Gnd")])
    if inh:
        wires(c, [("VD.outnod", "U.inh")])
    else:
        wires(c, [("U.inh", "G.Gnd")])
    if vin is not None:
        c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-160,-80", "Voltage=%g" % vin, "Out=true")
        wires(c, [("VI.outnod", "U.vcoin")])
    if r2:
        c("comp", "add", "Resistor", "--label", "R2", "--at", "-60,80", "--rot", "90", "Resistance=%g" % r2)
        wires(c, [("U.r2", "R2.lPin"), ("R2.rPin", "G.Gnd")])


def freq(sim, t="2ms"):
    sim.cli("sim", "run", "--for", t, timeout=600)
    return sim.cli("read", "F")["value"] or 0


def test(sim):
    c = sim.cli
    try:
        pll(sim, 10)
    except Exception as e:
        sim.check("PLL 4046 part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    within(sim, "4046 VCO, VCO IN 10 V (kHz)", freq(sim) / 1e3, 98, 102)
    c("sim", "stop")
    pll(sim, 5)
    within(sim, "4046 VCO, VCO IN 5 V (kHz)", freq(sim) / 1e3, 49, 51)
    within(sim, "4046 DEM OUT at VCO IN 5 V (V)", c("probe", "U.sf")[0]["voltage"], 3.1, 3.3)
    c("sim", "stop")
    pll(sim, 5, r2=1e5)
    within(sim, "4046 VCO, VCO IN 5 V, R2 100 k (kHz)", freq(sim) / 1e3, 58.8, 61.2)
    c("sim", "stop")
    pll(sim, 5, inh=True)
    within(sim, "4046 inhibited: VCO frequency (Hz)", freq(sim), 0, 0)
    c("sim", "stop")

    # Zener
    c("circuit", "new", "--discard")
    c("comp", "add", "Pll4046", "--label", "U", "--at", "0,0")
    c("comp", "add", "Fixed Voltage", "--label", "VD", "--at", "120,-80", "Voltage=10", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "120,160")
    c("comp", "add", "Resistor", "--label", "RZ", "--at", "160,40", "--rot", "90", "Resistance=90000")
    wires(c, [("U.vss", "G.Gnd"), ("VD.outnod", "RZ.lPin"), ("RZ.rPin", "U.zener"), ("VD.outnod", "U.vdd")])
    c("sim", "run", "--for", "1ms")
    within(sim, "4046 zener at ~50 uA (V)", c("probe", "U.zener")[0]["voltage"], 4.45, 6.15)
    c("sim", "stop")

    # PLL lock, phase comparator II: lead-lag filter, 30 kHz square in
    for pc in ("pc2", "pc1"):
        pll(sim)
        c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,-160", "Wave_Type=Square", "Freq=30 kHz",
          "Mid_Volt=5", "Semi_Ampli=5", "Always_On=true")
        c("comp", "add", "Resistor", "--label", "R3", "--at", "220,-120", "Resistance=10000")
        c("comp", "add", "Resistor", "--label", "R4", "--at", "-160,40", "--rot", "90",
          "Resistance=%g" % (1000 if pc == "pc2" else 0.001))
        c("comp", "add", "Capacitor", "--label", "C2", "--at", "-160,100", "--rot", "90",
          "Capacitance=%s" % ("100 nF" if pc == "pc2" else "10 nF"))
        wires(c, [("W.outnod", "U.sig"), ("U.vcoout", "U.comp"), ("U.%s" % pc, "R3.lPin"), ("R3.rPin", "U.vcoin"),
                  ("U.vcoin", "R4.lPin"), ("R4.rPin", "C2.lPin"), ("C2.rPin", "G.Gnd")])
        f = freq(sim, "20ms")
        vin, pp = 0, 0                            # Means over a period: PC1's ripple, and PC2's short
        for i in range(20):                       # correction pulses at the signal edges
            c("sim", "run", "--for", "1.7us")
            vin += c("probe", "U.vcoin")[0]["voltage"] / 20
            pp += c("probe", "U.pp")[0]["voltage"] > 5
        c("sim", "stop")
        within(sim, "PLL %s locked: VCO (kHz)" % pc.upper(), f / 1e3, 29.9, 30.1)
        within(sim, "PLL %s locked: VCO IN (V)" % pc.upper(), vin, 2.85, 3.15)
        if pc == "pc2":
            within(sim, "PLL PC2 locked: phase pulses high (of 20 samples)", pp, 18, 20)

    # NE566
    for vc, f0 in ((10.5, 2500), (9, 5000)):
        c("circuit", "new", "--discard")
        c("settings", "set", "reactStep=100ns")
        c("comp", "add", "Vco566", "--label", "U", "--at", "0,0")
        c("comp", "add", "Fixed Voltage", "--label", "VP", "--at", "120,-80", "Voltage=12", "Out=true")
        c("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "-160,-80", "Voltage=%g" % vc, "Out=true")
        c("comp", "add", "Ground", "--label", "G", "--at", "120,120")
        c("comp", "add", "Resistor", "--label", "R1", "--at", "-100,-40", "Resistance=10000")
        c("comp", "add", "Capacitor", "--label", "C1", "--at", "-100,60", "--rot", "90", "Capacitance=10 nF")
        c("comp", "add", "Frequency Meter", "--label", "F", "--at", "160,0")
        wires(c, [("VP.outnod", "U.vp"), ("U.gnd", "G.Gnd"), ("VC.outnod", "U.vc"), ("VP.outnod", "R1.lPin"),
                  ("R1.rPin", "U.r1"), ("C1.lPin", "U.c1"), ("C1.rPin", "G.Gnd"), ("F.lPin", "U.sq")])
        for n, p, y in (("RT", "U.tri", 40), ("RQ", "U.sq", 80)):
            c("comp", "add", "Resistor", "--label", n, "--at", "100,%d" % y, "--rot", "90", "Resistance=10000")
            wires(c, [(p, n + ".lPin"), (n + ".rPin", "G.Gnd")])
        f = freq(sim, "20ms")
        within(sim, "566 f0, VC %g V (Hz), formula %g" % (vc, f0), f, f0 * 0.98, f0 * 1.02)
        if vc == 10.5:
            lo, hi, slo, shi = 99, -99, 99, -99
            for i in range(40):
                c("sim", "run", "--for", "13us")
                v = c("probe", "U.tri")[0]["voltage"]
                lo, hi = min(lo, v), max(hi, v)
                v = c("probe", "U.sq")[0]["voltage"]
                slo, shi = min(slo, v), max(shi, v)
            within(sim, "566 triangle (Vpp)", hi - lo, 2.16, 2.64)
            within(sim, "566 square (Vpp)", shi - slo, 4.86, 5.94)
        c("sim", "stop")


run_test(test)
