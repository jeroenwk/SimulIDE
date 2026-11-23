"""Photodiode and Phototransistor against their datasheets (books: Scherz 5.18-5.29, 8.41).

SimulIDE had only the LDR as a light sensor. Both parts have a light level in lux (dial,
as the LDR) and a photocurrent I = Sensitivity * lux (docs/PHOTO.md).

Photodiode, model BPW34 (Vishay doc. 81521 rev. 2.1, 23-Aug-11, Basic Characteristics p. 2):
- short-circuit current Ik = 70 uA typ at EA = 1 klx;
- reverse light current Ira = 75 uA typ at EA = 1 klx, VR = 5 V (model: 70 uA + shunt);
- open-circuit voltage Vo = 350 mV typ at Ee = 1 mW/cm2, where Ik = 47 uA: the same
  photocurrent as 671 lx of illuminant A (47/70 klx) [INFERRED equivalence];
- reverse dark current Iro = 2 nA typ, 30 nA max at VR = 10 V;
Phototransistor, model BPW85A/B/C (Vishay doc. 81531 rev. 2.1, 04-Aug-14, p. 2):
- collector light current at Ee = 1 mW/cm2 (= 671 lx, as above), VCE = 5 V:
  A 0.8..2.5 mA, B 1.5..4 mA, C 3..8 mA;
- dark current ICEO <= 200 nA at VCE = 20 V (steady: the power-up step charges C_cb
  through the base, times the gain, for some ms, as on a real part);
- VCEsat <= 0.3 V at IC = 0.1 mA, Ee = 1 mW/cm2;
- turn-on / turn-off 2.0 / 2.3 us typ at VS = 5 V, IC = 5 mA, RL = 100 ohm: within 3x.
Model: BPW85C survives save + reopen; Sensitivity set by hand makes it Custom.
"""

import os
import tempfile
from harness import run_test

EE1 = 1000 * 47 / 70        # lx giving the photocurrent of Ee = 1 mW/cm2 at 950 nm


def within(sim, name, value, lo, hi):
    sim.check("%s = %.4g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def diode(sim, vr, rl, lux):
    """Photodiode, cathode at +vr, anode to ground through rl -> voltage at the anode."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,160")
    c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0", "Lux=%g" % lux, "Max_Lux=100000")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "-80,80", "--rot", "90", "Resistance=%g" % rl)
    c("wire", "add", "D.anode", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    if vr > 0:
        c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "120,-80", "Voltage=%g" % vr, "Out=true")
        c("wire", "add", "V.outnod", "D.cathode")
    else:
        c("wire", "add", "D.cathode", "G.Gnd")
    c("sim", "run", "--for", "1ms")
    v = c("probe", "D.anode")[0]["voltage"]
    c("sim", "stop")
    return v


def transistor(sim, vs, rl, lux, model="BPW85B"):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,160")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-80,-160", "Voltage=%g" % vs, "Out=true")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "8,-80", "--rot", "90", "Resistance=%g" % rl)
    c("comp", "add", "Phototransistor", "--label", "Q", "--at", "0,0", "Lux=%g" % lux,
      "Max_Lux=100000", "Model=%s" % model)
    c("wire", "add", "V.outnod", "RL.lPin")
    c("wire", "add", "RL.rPin", "Q.collector")
    c("wire", "add", "Q.emitter", "G.Gnd")


def test(sim):
    c = sim.cli
    try:
        c("circuit", "new", "--discard")
        c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0")
    except Exception as e:
        sim.check("Photodiode part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return

    # BPW34
    v = diode(sim, 0, 10, 1000)
    within(sim, "BPW34 Ik at 1 klx (uA)", v / 10 * 1e6, 63, 77)
    v = diode(sim, 0, 1e9, EE1)
    within(sim, "BPW34 Vo at 1 mW/cm2 (mV)", v * 1e3, 300, 400)
    v = diode(sim, 5, 1000, 1000)
    within(sim, "BPW34 Ira at 1 klx, 5 V (uA)", v / 1000 * 1e6, 60, 90)
    v = diode(sim, 10, 1e6, 0)
    within(sim, "BPW34 Iro at 10 V, dark (nA)", v / 1e6 * 1e9, 0, 30)
    v = diode(sim, 0, 1000, 10)
    within(sim, "BPW34 Ik at 10 lx (uA, linear)", v / 1000 * 1e6, 0.63, 0.77)

    # BPW85: light current per class
    for model, lo, hi in (("BPW85A", 0.8, 2.5), ("BPW85B", 1.5, 4.0), ("BPW85C", 3.0, 8.0)):
        transistor(sim, 5, 10, EE1, model)
        c("sim", "run", "--for", "1ms")
        i = (5 - c("probe", "Q.collector")[0]["voltage"]) / 10
        c("sim", "stop")
        within(sim, "%s Ica at 1 mW/cm2 (mA)" % model, i * 1e3, lo, hi)

    transistor(sim, 20, 1e4, 0)          # after the power-up charge of C_cb (x gain) has gone
    c("sim", "run", "--for", "100ms", timeout=300)
    i = (20 - c("probe", "Q.collector")[0]["voltage"]) / 1e4
    c("sim", "stop")
    within(sim, "BPW85 ICEO at 20 V (nA)", i * 1e9, 0, 200)

    transistor(sim, 5, 4.9e4, EE1)
    c("sim", "run", "--for", "1ms")
    v = c("probe", "Q.collector")[0]["voltage"]
    c("sim", "stop")
    within(sim, "BPW85 VCEsat at 0.1 mA (V)", v, 0, 0.3)

    # Switching: light for IC = 5 mA (a little more, so it saturates), RL 100 ohm, 5 V
    lux = 5e-3 / (500 * 7.3e-9)
    transistor(sim, 5, 100, 0)
    c("sim", "run", "--for", "100us")
    c("comp", "set", "Q", "Lux=%g" % lux)
    t, ton = 0, None
    while t < 30e-6:
        c("sim", "run", "--for", "100ns")
        t += 100e-9
        if c("probe", "Q.collector")[0]["voltage"] < 5 - 0.9 * 0.5:
            ton = t
            break
    c("sim", "run", "--for", "50us")
    c("comp", "set", "Q", "Lux=0")
    t, toff = 0, None
    while t < 30e-6:
        c("sim", "run", "--for", "100ns")
        t += 100e-9
        if c("probe", "Q.collector")[0]["voltage"] > 5 - 0.1 * 0.5:
            toff = t
            break
    c("sim", "stop")
    within(sim, "BPW85 ton (us)", (ton or 1) * 1e6, 0.67, 6)
    within(sim, "BPW85 toff (us)", (toff or 1) * 1e6, 0.77, 7)

    # Base pin: shown but left open = no base pin
    transistor(sim, 5, 10, EE1)
    c("comp", "set", "Q", "Base_Pin=true")
    c("sim", "run", "--for", "1ms")
    i = (5 - c("probe", "Q.collector")[0]["voltage"]) / 10
    c("sim", "stop")
    within(sim, "BPW85B base pin open: Ica (mA)", i * 1e3, 1.5, 4.0)

    transistor(sim, 5, 10, EE1, "BPW85C")
    p = os.path.join(tempfile.mkdtemp(prefix="photo-"), "q.sim1")
    c("circuit", "save", p)
    c("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in c("comp", "get", "Q")["properties"]}
    sim.check("reopened Model (%s)" % props.get("Model"), int(props.get("Model") == "BPW85C"), 1, absolute=0)
    c("comp", "set", "Q", "Sensitivity=20 nA")
    props = {q["name"]: q["value"] for q in c("comp", "get", "Q")["properties"]}
    sim.check("a parameter set by hand -> Custom (%s)" % props.get("Model"), int(props.get("Model") == "Custom"), 1,
              absolute=0)


run_test(test)
