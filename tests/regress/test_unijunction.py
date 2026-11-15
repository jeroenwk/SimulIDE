"""PUT and UJT parts against their datasheets' guaranteed limits (create-circuit lim-parts).

SimulIDE had no programmable unijunction transistor (PUT) and no unijunction transistor
(UJT): book circuits with a 2N6027 or a 2N2646 (relaxation oscillators, ramp generators,
SCR triggers) were skipped or built from two BJTs by hand. Both are now native parts with
a Model list (docs/UNIJUNCTION.md).

The V-I curve is traced with a current source on the anode (emitter): voltage is a
single-valued function of current, so the peak point (VP at IP) is the highest voltage
and the valley point (VV at IV) the lowest after it.

2N6027 / 2N6028 (onsemi 2N6027/D rev 6, 2006, Electrical Characteristics; VS = 10 V on
the gate through RG, as its Fig. 1B):
- 2N6027: IP <= 2 uA (RG 1 M), <= 5 uA (10 k); VT = VP - VS 0.2..1.6 V (1 M),
  0.2..0.6 V (10 k); IV <= 50 uA (1 M), >= 70 uA (10 k), >= 1.5 mA (200 ohm);
  VF <= 1.5 V at 50 mA.
- 2N6028: IP <= 0.15 uA (1 M), <= 1.0 uA (10 k); VT 0.2..0.6 V; IV <= 25 uA (1 M),
  >= 25 uA (10 k), >= 1.0 mA (200 ohm).
2N2646 (Comset datasheet): RBB 4.7..9.1 k at VB2B1 3 V; peak point VP = eta*VBB + VD with
eta 0.56..0.75 at 10 V, so VP between 0.56*10 + 0.4 and 0.75*10 + 0.7 V (VD a diode drop);
IV >= 4 mA at 20 V (100 ohm in series with B2); IP <= 5 uA at 25 V;
VE(sat) <= 2.5 V at 50 mA, 10 V.

Oscillators (the books' circuits), against the textbook formula with the part's own
model values, within 10 %:
- UJT: 12 V on B2, 47 k + 100 nF on the emitter, B1 to ground:
  f = 1 / (R C ln((VBB - VV) / (VBB - VP))), VP = 0.65*12 + 0.45, VV = 0.7 -> 193 Hz.
- PUT: 12 V, gate on a 10 k / 10 k divider (VS 6 V), 100 k + 100 nF on the anode:
  VP = 6 + 0.45, VV = 0.7 -> 1 / (0.01 ln(11.3 / 5.55)) = 141 Hz.
Model: choosing 2N6028 survives save + reopen; changing a parameter by hand makes it Custom.
"""

import math
import os
import tempfile
from harness import run_test


def build(sim, kind, rs, vs, props=()):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Current Source", "--label", "I1", "--at", "-200,0", "Value_Amp=1e-9",
      "MaxValue=1", "Running=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,160")
    c("comp", "add", "Fixed Voltage", "--label", "VS", "--at", "-120,-120", "Voltage=%g" % vs, "Out=true")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "-60,-120", "Resistance=%g" % rs)
    c("comp", "add", kind, "--label", "Q", "--at", "0,0", *props)
    c("wire", "add", "VS.outnod", "RS.lPin")
    if kind == "PUT":
        c("wire", "add", "RS.rPin", "Q.gate")
        c("wire", "add", "I1.outPin", "Q.anode")
        c("wire", "add", "Q.cathode", "G.Gnd")
        return "Q.anode"
    c("wire", "add", "RS.rPin", "Q.base2")
    c("wire", "add", "I1.outPin", "Q.emitter")
    c("wire", "add", "Q.base1", "G.Gnd")
    return "Q.emitter"


def curve(sim, kind, rs, vs, props=(), imin=1e-9, imax=0.06):
    """-> (VP, IP, VV, IV, V at 50 mA)"""
    pin = build(sim, kind, rs, vs, props)
    sim.cli("sim", "start")
    pts, i = [], imin
    while i < imax:
        sim.cli("comp", "set", "I1", "Value_Amp=%g" % i)
        sim.cli("sim", "run", "--for", "200us")
        pts.append((i, sim.cli("probe", pin)[0]["voltage"]))
        i *= 1.15
    sim.cli("comp", "set", "I1", "Value_Amp=0.05")
    sim.cli("sim", "run", "--for", "200us")
    v50 = sim.cli("probe", pin)[0]["voltage"]
    sim.cli("sim", "stop")
    k = max(range(len(pts)), key=lambda j: pts[j][1])
    v = min(range(k, len(pts)), key=lambda j: pts[j][1])
    return pts[k][1], pts[k][0], pts[v][1], pts[v][0], v50


def within(sim, name, value, lo, hi):
    ok = lo <= value <= hi
    sim.check("%s = %.4g in [%g, %g]" % (name, value, lo, hi), int(ok), 1, absolute=0)


def oscillator(sim, kind):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-200,-160", "Voltage=12", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,200")
    c("comp", "add", "Resistor", "--label", "RT", "--at", "-120,-80", "--rot", "90",
      "Resistance=%g" % (47e3 if kind == "UJT" else 100e3))
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-120,80", "--rot", "90", "Capacitance=100 nF")
    c("comp", "add", "Frequency Meter", "--label", "F", "--at", "-260,40")
    c("comp", "add", kind, "--label", "Q", "--at", "0,0")
    c("wire", "add", "V.outnod", "RT.lPin")
    c("wire", "add", "C1.rPin", "G.Gnd")
    if kind == "UJT":
        a = "Q.emitter"
        c("wire", "add", "V.outnod", "Q.base2")
        c("wire", "add", "Q.base1", "G.Gnd")
    else:
        a = "Q.anode"
        c("comp", "add", "Resistor", "--label", "R1", "--at", "80,-80", "--rot", "90", "Resistance=10000")
        c("comp", "add", "Resistor", "--label", "R2", "--at", "80,80", "--rot", "90", "Resistance=10000")
        c("wire", "add", "V.outnod", "R1.lPin")
        c("wire", "add", "R1.rPin", "R2.lPin")
        c("wire", "add", "R2.rPin", "G.Gnd")
        c("wire", "add", "R1.rPin", "Q.gate")
        c("wire", "add", "Q.cathode", "G.Gnd")
    c("wire", "add", "RT.rPin", a)
    c("wire", "add", "C1.lPin", a)
    c("wire", "add", "F.lPin", a)
    c("sim", "run", "--for", "200ms", timeout=300)
    f = sim.cli("read", "F").get("value")
    sim.cli("sim", "stop")
    return float(str(f).split()[0]) if f is not None else 0.0


def test(sim):
    try:
        build(sim, "PUT", 1e4, 10)
    except Exception as e:                                  # no PUT part yet
        sim.check("PUT part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return

    for model, rows in (
        ("2N6027", ((1e6, 2e-6, (0.2, 1.6), (0, 50e-6)),
                    (1e4, 5e-6, (0.2, 0.6), (70e-6, 1)),
                    (200, None, None, (1.5e-3, 1)))),
        ("2N6028", ((1e6, 0.15e-6, (0.2, 0.6), (0, 25e-6)),
                    (1e4, 1.0e-6, (0.2, 0.6), (25e-6, 1)),
                    (200, None, None, (1.0e-3, 1))))):
        for rg, ip_max, vt, iv in rows:
            vp, ip, vv, ivm, v50 = curve(sim, "PUT", rg, 10, ["Model=%s" % model])
            tag = "%s RG %g" % (model, rg)
            if ip_max:
                within(sim, tag + " IP (A)", ip, 0, ip_max)
                within(sim, tag + " VT (V)", vp - 10, *vt)
            within(sim, tag + " IV (A)", ivm, *iv)
            if model == "2N6027" and rg == 1e4:
                within(sim, "2N6027 VF at 50 mA (V)", v50, 0.3, 1.5)

    vp, ip, vv, ivm, v50 = curve(sim, "UJT", 0.001, 10, imin=1e-7)
    within(sim, "2N2646 VP at 10 V (V)", vp, 0.56 * 10 + 0.4, 0.75 * 10 + 0.7)
    within(sim, "2N2646 VE(sat) at 50 mA (V)", v50, 0.3, 2.5)
    vp, ip, vv, ivm, v50 = curve(sim, "UJT", 100, 20, imin=1e-7)
    within(sim, "2N2646 IV at 20 V (A)", ivm, 4e-3, 1)
    vp, ip, vv, ivm, v50 = curve(sim, "UJT", 0.001, 25, imin=1e-7)
    within(sim, "2N2646 IP at 25 V (A)", ip, 0, 5e-6)

    build(sim, "UJT", 0.001, 3)                                 # RBB: emitter open, 3 V across B2-B1
    sim.cli("comp", "set", "I1", "Value_Amp=0")
    sim.cli("sim", "run", "--for", "100us")
    i2 = sim.cli("probe", "Q.base2")[0]["current"]
    sim.cli("sim", "stop")
    within(sim, "2N2646 RBB (ohm)", 3 / abs(i2) if i2 else 0, 4.7e3, 9.1e3)

    f = oscillator(sim, "UJT")
    vp, vv = 0.65 * 12 + 0.45, 0.7
    want = 1 / (47e3 * 100e-9 * math.log((12 - vv) / (12 - vp)))
    sim.check("UJT oscillator (Hz)", f, want, tol=0.10)
    f = oscillator(sim, "PUT")
    want = 1 / (100e3 * 100e-9 * math.log((12 - 0.7) / (12 - 6.45)))
    sim.check("PUT oscillator (Hz)", f, want, tol=0.10)

    build(sim, "PUT", 1e4, 10, ["Model=2N6028"])
    p = os.path.join(tempfile.mkdtemp(prefix="unij-"), "q.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "Q")["properties"]}
    sim.check("reopened Model (%s)" % props.get("Model"), int(props.get("Model") == "2N6028"), 1, absolute=0)
    sim.cli("comp", "set", "Q", "GainN=20")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "Q")["properties"]}
    sim.check("a parameter set by hand -> Custom (%s)" % props.get("Model"), int(props.get("Model") == "Custom"), 1,
              absolute=0)


run_test(test)
