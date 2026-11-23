"""Optocoupler, Opto-Triac and Opto Logic against their datasheets (AoE 12.86-12.94, Scherz 5.30-5.33).

SimulIDE had no optocoupler; books' isolators, SSRs and triac drivers were skipped.
Three parts with an infrared LED driving an isolated output (docs/OPTOCOUPLER.md).

Optocoupler, model 4N35 (Vishay doc. 81181 rev. 1.2, 07-Jan-10, p. 154-155):
- VF 1.3 V typ, 1.5 V max at IF = 10 mA;
- CTR >= 100 % at IF = 10 mA, VCE = 10 V;
- ICEO 5 nA typ, 50 nA max at VCE = 10 V, IF = 0 (steady);
- ton, toff 10 us typ at VCC = 10 V, IC = 2 mA, RL = 100 ohm: within 3x.
Opto-Triac (onsemi MOC3023M/D rev. 3, 2023, and MOC3043M/D rev. 4, 2022, p. 3):
- MOC3021: fires at IF = IFT max 15 mA with 3 V on the main terminals; IF = 2 mA does not
  (below any part's trigger current, here 8 mA [INFERRED typical]);
- VTM 1.8 V typ, 3 V max at ITM = 100 mA;
- on AC (50 Hz, 100 V peak, 1 k): conducts both half cycles while the LED is on, stops at the
  next zero crossing after it goes off (holding current 100 uA);
- MOC3041 zero-cross: the LED switched on at the peak does not fire it (|V| above the
  inhibit voltage, 20 V max); it fires after the next zero crossing.
Opto Logic, model 6N137 (Vishay doc. 84732 rev. 2.2, 15-Mar-2023, p. 3-4), VCC 5 V,
RL 350 ohm to VCC:
- VOL 0.2 V typ, 0.6 V max with IF above threshold (13 mA through RL here); high with IF = 0;
- input threshold ITH 2.4 mA typ, 5 mA max: low at 5 mA, high at 1 mA;
- enable low (0 V) holds the output high;
- tPHL 50 ns, tPLH 48 ns typ, 100 ns max;
- supply current 3.3 mA typ (VE = VCC, IF = 0).
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.4g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def led(sim, vin, r):
    """LED driven from a fixed source 'VI' (vin) through 'RI' (r): anode = RI.rPin."""
    c = sim.cli
    c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-200,-120", "Voltage=%g" % vin, "Out=true")
    c("comp", "add", "Resistor", "--label", "RI", "--at", "-120,-8", "Resistance=%g" % r)
    c("comp", "add", "Ground", "--label", "GI", "--at", "-80,120")
    c("wire", "add", "VI.outnod", "RI.lPin")
    c("wire", "add", "RI.rPin", "U.anode")
    c("wire", "add", "U.cathode", "GI.Gnd")


def opto4n35(sim, vin, r, vcc, rl):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Optocoupler", "--label", "U", "--at", "0,0")
    led(sim, vin, r)
    c("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "200,-120", "Voltage=%g" % vcc, "Out=true")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "120,-60", "--rot", "90", "Resistance=%g" % rl)
    c("comp", "add", "Ground", "--label", "GO", "--at", "120,120")
    c("wire", "add", "VC.outnod", "RL.lPin")
    c("wire", "add", "RL.rPin", "U.collector")
    c("wire", "add", "U.emitter", "GO.Gnd")


def edge(sim, probe, cond, limit, step):
    t = 0
    while t < limit:
        sim.cli("sim", "run", "--for", "%gns" % (step * 1e9))
        t += step
        if cond(sim.cli("probe", probe)[0]["voltage"]):
            return t
    return None


def triac(sim, model, src):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "OptoTriac", "--label", "U", "--at", "0,0", "Model=%s" % model)
    led(sim, 5, 330)
    c("comp", "add", "Ground", "--label", "GO", "--at", "120,120")
    c("wire", "add", "U.mt1", "GO.Gnd")
    if src == "ac":
        c("comp", "add", "WaveGen", "--label", "W", "--at", "200,-120", "Wave_Type=Sine", "Freq=50 Hz",
          "Mid_Volt=0", "Semi_Ampli=100", "Always_On=true")
        c("comp", "add", "Resistor", "--label", "RL", "--at", "120,-60", "--rot", "90", "Resistance=1000")
        c("wire", "add", "W.outnod", "RL.lPin")
    else:
        c("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "200,-120", "Voltage=%g" % src[0], "Out=true")
        c("comp", "add", "Resistor", "--label", "RL", "--at", "120,-60", "--rot", "90", "Resistance=%g" % src[1])
        c("wire", "add", "VC.outnod", "RL.lPin")
    c("wire", "add", "RL.rPin", "U.mt2")


def test(sim):
    c = sim.cli
    try:
        c("circuit", "new", "--discard")
        c("comp", "add", "Optocoupler", "--label", "U", "--at", "0,0")
    except Exception as e:
        sim.check("Optocoupler part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return

    # 4N35 ------------------------------------------------------------------
    opto4n35(sim, 5, 370, 10, 10)                 # IF ~ (5 - 1.3)/370 = 10 mA
    c("sim", "run", "--for", "10ms")
    va = c("probe", "U.anode")[0]["voltage"]
    i_f = (5 - va) / 370
    ic = (10 - c("probe", "U.collector")[0]["voltage"]) / 10
    c("sim", "stop")
    within(sim, "4N35 VF at %.1f mA (V)" % (i_f * 1e3), va, 1.15, 1.5)
    within(sim, "4N35 CTR at 10 mA, VCE 10 V (%)", 100 * ic / i_f, 100, 300)

    opto4n35(sim, 0, 370, 10, 1e4)
    c("sim", "run", "--for", "200ms", timeout=300)
    i = (10 - c("probe", "U.collector")[0]["voltage"]) / 1e4
    c("sim", "stop")
    within(sim, "4N35 ICEO at 10 V (nA)", i * 1e9, 0, 50)

    rin = (5 - 1.2) / (2e-3 / 1.5)                # IC = 2 mA through 100 ohm at CTR 1.5
    opto4n35(sim, 5, rin, 10, 100)
    c("comp", "set", "VI", "Out=false")
    c("sim", "run", "--for", "1ms")
    c("comp", "set", "VI", "Out=true")
    ton = edge(sim, "U.collector", lambda v: v < 10 - 0.9 * 0.2, 60e-6, 0.5e-6)
    c("sim", "run", "--for", "200us")
    c("comp", "set", "VI", "Out=false")
    toff = edge(sim, "U.collector", lambda v: v > 10 - 0.1 * 0.2, 60e-6, 0.5e-6)
    c("sim", "stop")
    within(sim, "4N35 ton (us)", (ton or 1) * 1e6, 3.3, 30)
    within(sim, "4N35 toff (us)", (toff or 1) * 1e6, 3.3, 30)

    # MOC3021 / MOC3041 ------------------------------------------------------
    for rin, fires in ((5 - 1.2) / 15e-3, 1), ((5 - 1.1) / 2e-3, 0):
        triac(sim, "MOC3021", (3, 100))
        c("comp", "set", "RI", "Resistance=%g" % rin)
        c("sim", "run", "--for", "5ms")
        v = c("probe", "U.mt2")[0]["voltage"]
        c("sim", "stop")
        sim.check("MOC3021 IF %.0f mA, 3 V: %s (MT2 %.2f V)" % (3.8 / rin * 1e3, "fires" if fires else "blocks", v),
                  int((v < 2.5) == bool(fires)), 1, absolute=0)

    triac(sim, "MOC3021", (12, 102))
    c("comp", "set", "RI", "Resistance=%g" % (3.8 / 15e-3))
    c("sim", "run", "--for", "5ms")
    v = c("probe", "U.mt2")[0]["voltage"]
    c("sim", "stop")
    within(sim, "MOC3021 VTM at %.0f mA (V)" % ((12 - v) / 102 * 1e3), v, 1.0, 3.0)

    # AC, 50 Hz: LED on for 0..30 ms, off after
    triac(sim, "MOC3021", "ac")
    c("comp", "set", "RI", "Resistance=%g" % (3.8 / 15e-3))
    c("sim", "run", "--for", "5ms")               # positive peak
    v1 = c("probe", "U.mt2")[0]["voltage"]
    c("sim", "run", "--for", "10ms")              # negative peak
    v2 = c("probe", "U.mt2")[0]["voltage"]
    c("sim", "run", "--for", "10ms")              # 25 ms: positive peak again
    c("comp", "set", "VI", "Out=false")           # LED off while conducting
    c("sim", "run", "--for", "2ms")
    v3 = c("probe", "U.mt2")[0]["voltage"]
    c("sim", "run", "--for", "10ms")              # 37 ms: after the zero crossing, blocking
    v4 = c("probe", "U.mt2")[0]["voltage"]
    c("sim", "stop")
    sim.check("MOC3021 AC: on at + peak (%.1f V)" % v1, int(abs(v1) < 3), 1, absolute=0)
    sim.check("MOC3021 AC: on at - peak (%.1f V)" % v2, int(abs(v2) < 3), 1, absolute=0)
    sim.check("MOC3021 AC: LED off, latched to the zero crossing (%.1f V)" % v3, int(abs(v3) < 3), 1, absolute=0)
    sim.check("MOC3021 AC: blocks after it (%.1f V)" % v4, int(abs(v4) > 50), 1, absolute=0)

    for model, at_peak in (("MOC3021", 1), ("MOC3041", 0)):
        triac(sim, model, "ac")
        c("comp", "set", "VI", "Out=false")
        c("sim", "run", "--for", "5ms")           # at the positive peak
        c("comp", "set", "VI", "Out=true")
        c("comp", "set", "RI", "Resistance=%g" % (3.8 / 15e-3))
        c("sim", "run", "--for", "1ms")
        v1 = c("probe", "U.mt2")[0]["voltage"]
        c("sim", "run", "--for", "8ms")           # 14 ms: after the zero crossing at 10 ms
        v2 = c("probe", "U.mt2")[0]["voltage"]
        c("sim", "stop")
        sim.check("%s LED on at the peak: %s (%.1f V)" % (model, "fires" if at_peak else "waits", v1),
                  int((abs(v1) < 3) == bool(at_peak)), 1, absolute=0)
        sim.check("%s after the zero crossing: on (%.1f V)" % (model, v2), int(abs(v2) < 3), 1, absolute=0)

    # 6N137 -------------------------------------------------------------------
    def logic(rin, ve=None):
        c("circuit", "new", "--discard")
        c("comp", "add", "OptoLogic", "--label", "U", "--at", "0,0")
        led(sim, 5, rin)
        c("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "200,-120", "Voltage=5", "Out=true")
        c("comp", "add", "Resistor", "--label", "RL", "--at", "120,-60", "--rot", "90", "Resistance=350")
        c("comp", "add", "Ground", "--label", "GO", "--at", "120,120")
        c("wire", "add", "VC.outnod", "RL.lPin")
        c("wire", "add", "VC.outnod", "U.vcc")
        c("wire", "add", "RL.rPin", "U.out")
        c("wire", "add", "U.gnd", "GO.Gnd")
        if ve is not None:
            c("comp", "add", "Fixed Voltage", "--label", "VE", "--at", "200,-40", "Voltage=%g" % ve, "Out=true")
            c("wire", "add", "VE.outnod", "U.enable")

    for rin, ma, low in ((480, 7.5, 1), (720, 5, 1), (3800, 1, 0)):
        logic(rin)
        c("sim", "run", "--for", "1ms")
        v = c("probe", "U.out")[0]["voltage"]
        c("sim", "stop")
        sim.check("6N137 IF %g mA: output %s (%.2f V)" % (ma, "low" if low else "high", v),
                  int((v < 0.6) if low else (v > 4.5)), 1, absolute=0)
        if ma == 7.5:
            within(sim, "6N137 VOL with 13 mA (V)", v, 0, 0.6)

    logic(480, 0)
    c("sim", "run", "--for", "1ms")
    v = c("probe", "U.out")[0]["voltage"]
    c("sim", "stop")
    sim.check("6N137 enable low: output high (%.2f V)" % v, int(v > 4.5), 1, absolute=0)

    logic(1e9)                                    # LED off: supply current
    c("sim", "run", "--for", "1ms")
    icc = abs(c("probe", "VC.outnod")[0]["current"])   # RL carries ~0 with the output off
    c("sim", "stop")
    within(sim, "6N137 ICC, IF = 0 (mA)", icc * 1e3, 2, 6)

    logic(480)
    c("comp", "set", "VI", "Out=false")
    c("sim", "run", "--for", "100us")
    c("comp", "set", "VI", "Out=true")
    tphl = edge(sim, "U.out", lambda v: v < 1.5, 500e-9, 5e-9)
    c("sim", "run", "--for", "10us")
    c("comp", "set", "VI", "Out=false")
    tplh = edge(sim, "U.out", lambda v: v > 1.5, 500e-9, 5e-9)
    c("sim", "stop")
    within(sim, "6N137 tPHL (ns)", (tphl or 1) * 1e9, 20, 100)
    within(sim, "6N137 tPLH (ns)", (tplh or 1) * 1e9, 20, 100)


run_test(test)
