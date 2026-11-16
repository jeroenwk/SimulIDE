"""One-shot timing laws: 74XX121 and CD4098 (create-circuit #277, #278, #279).

- 74XX121: the pulse ended when the Rext/Cext node crossed the sense inverter's 3.5 V
  threshold, 1.15 RC instead of the real 0.69 RC (RC ln 2, #277); on release Cext kicked that
  node to 8 V, which decayed through Rext, so close triggers gave short pulses (#279).
- CD4098: the RC node was driven by a push-pull buffer, so Cext charged through its 40 ohm
  output and Rext did nothing (no pulse at 100k/10n, 4 us at 10k/100n, #278); the real part
  discharges Cx at the trigger and Rx charges it: PW = 0.5 Rx Cx.

Right result: 74XX121 at 10k / 100 nF: 0.69 ms +-3 %, and the RC node stays below 6 V;
CD4098 (both halves) at 100k / 10 nF and 10k / 100 nF: 0.50 ms +-4 %.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def widths(samples):
    hi, out, t0 = False, [], None
    for t, v in samples:
        if not hi and v > 2.5:
            hi, t0 = True, t
        elif hi and v < 2.5:
            hi = False
            out.append(t - t0)
    return out


def pulse(sim, part, rext, cext, half=1):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "VDD", "--at", "-200,-100", "Voltage=5")
    c("comp", "add", "Ground", "--label", "G", "--at", "-200,200")
    c("comp", "add", "Clock", "--label", "CK", "--at", "-300,0", "Freq=100", "Always_On=true")
    c("comp", "add", "Resistor", "--label", "RX", "--at", "-100,-60", "Resistance=%g" % rext)
    c("comp", "add", "Capacitor", "--label", "CX", "--at", "-100,100", "Capacitance=%g" % cext)
    c("comp", "add", "Oscope", "--label", "SC", "--at", "300,0", "InputImped=10G")
    if part == "74121":
        c("comp", "add", "74XX121", "--label", "U", "--at", "0,0")
        c("wire", "add", "U.!A1", "G.Gnd")
        c("wire", "add", "U.!A2", "G.Gnd")
        c("wire", "add", "CK.outnod", "U.B")
        rc, q, cpin = "U.RC", "U.Q", "U.C"
    else:
        c("comp", "add", "CD4098", "--label", "U", "--at", "0,0")
        c("wire", "add", "CK.outnod", "U.TR%d" % half)
        c("wire", "add", "VDD.outnod", "U.!TR%d" % half)
        c("wire", "add", "VDD.outnod", "U.!R%d" % half)
        rc, q, cpin = "U.RC%d" % half, "U.Q%d" % half, "U.C%d" % half
    c("wire", "add", "VDD.outnod", "RX.lPin")
    c("wire", "add", "RX.rPin", rc)
    c("wire", "add", "CX.lPin", cpin)
    c("wire", "add", "CX.rPin", rc)
    c("wire", "add", q, "SC.Pin0")
    c("wire", "add", rc, "SC.Pin1")
    sim.run("40ms")
    w = widths(c("scope", "data", "SC", "--ch", 0, "--max", 200000)["channels"][0]["samples"])
    vmax = max(v for _, v in c("scope", "data", "SC", "--ch", 1, "--max", 200000)["channels"][0]["samples"])
    return (w[-1] if len(w) > 1 else 0.0), vmax


def test(sim):
    w, vmax = pulse(sim, "74121", 10e3, 100e-9)
    sim.check("74XX121 10k/100n pulse (RC ln 2)", w, 0.693e-3, tol=0.03)
    sim.check("74XX121 Rext/Cext node peak below 6 V (%.2f V)" % vmax, int(vmax < 6), 1, absolute=0)
    for half in (1, 2):
        for r, cx in ((100e3, 10e-9), (10e3, 100e-9)):
            w, _ = pulse(sim, "4098", r, cx, half)
            sim.check("CD4098 half %d %gk/%gn pulse (0.5 RC)" % (half, r / 1e3, cx * 1e9), w, 0.5e-3, tol=0.04)


run_test(test)
