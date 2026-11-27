"""The 4046 and 566 VCO frequencies do not depend on the reactive step (create-circuit #653,
#660, #738).

Both reversed the timing current as soon as the capacitor's node passed the threshold. The
solver's capacitor commits its charge only at its own steps (the node shows where the step will
end), so the reversal undid that step: each half period ended one step early. A 4046 at 10 kHz
read 10417 Hz at the default 1 us (4 % high), a 566 1 % high; a 4046 driven by a WaveGen
jittered with the source's Steps (#738).
Expected (formulas of docs/PLL_VCO.md; period from rising edges of the square / VCO out):
- 4046, VDD 10, VCO IN 5.005 V, R1 100k, C1 0.5 nF: f = I / (10 V C1) = 10010 Hz within 0.1 %
  at reactStep 1 us and 100 ns;
- 566, V+ 12, VC 10 V, R1 10k, C1 10 nF: f = 2 (V+ - VC) / (R1 C1 V+) = 3333.3 Hz within 0.1 %
  at 1 us and 100 ns;
- 4046 at ~60 kHz on a DC control: period spread below one step (sd < 40 ns at 40 ns).
"""
import statistics
from harness import run_test


def edges(c, level):
    d = c("scope", "data", "SC", "--ch", "0", "--max", "0")
    pts = d["channels"][0]["samples"] if "channels" in d else d[0]["samples"]
    return [t1 for (t0, v0), (t1, v1) in zip(pts, pts[1:]) if v0 < level <= v1]


def periods(c, rstep, timediv, span, level):
    c("settings", "set", "reactStep=%s" % rstep)
    c("scope", "set", "SC", "--timediv", timediv)
    c("sim", "run", "--for", span)
    t = edges(c, level)
    c("sim", "stop")
    return [b - a for a, b in zip(t, t[1:])][2:]


def pll(c, vin, r1, cap):
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "VDD", "--at", "200,-120", "Voltage=10")
    c("comp", "add", "Pll4046", "--label", "U", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-120,40", "Resistance=%g" % r1)
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-120,0", "Capacitance=%g" % cap)
    c("comp", "add", "Ground", "--label", "G", "--at", "100,120")
    c("comp", "add", "Rail", "--label", "VC", "--at", "-200,-80", "Voltage=%g" % vin)
    c("comp", "add", "Oscope", "--label", "SC", "--at", "300,0")
    for a, b in (("VDD.outnod", "U.vdd"), ("U.vss", "G.Gnd"), ("U.r1", "R1.rPin"), ("R1.lPin", "G.Gnd"),
                 ("U.c1a", "C1.rPin"), ("U.c1b", "C1.lPin"), ("U.inh", "G.Gnd"), ("VC.outnod", "U.vcoin"),
                 ("U.vcoout", "SC.Pin0"), ("SC.PinG", "G.Gnd")):
        c("wire", "add", a, b)


def vco(c):
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "VP", "--at", "200,-120", "Voltage=12")
    c("comp", "add", "Vco566", "--label", "U", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-120,0", "Resistance=10k")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-120,60", "Capacitance=10n")
    c("comp", "add", "Ground", "--label", "G", "--at", "100,120")
    c("comp", "add", "Rail", "--label", "VC", "--at", "-200,-80", "Voltage=10")
    c("comp", "add", "Oscope", "--label", "SC", "--at", "300,0")
    for a, b in (("VP.outnod", "U.vp"), ("U.gnd", "G.Gnd"), ("VP.outnod", "R1.lPin"), ("R1.rPin", "U.r1"),
                 ("U.c1", "C1.lPin"), ("C1.rPin", "G.Gnd"), ("VC.outnod", "U.vc"), ("U.sq", "SC.Pin0"),
                 ("SC.PinG", "G.Gnd")):
        c("wire", "add", a, b)


def test(sim):
    c = sim.cli
    for rs in ("1us", "100ns"):
        pll(c, 5.005, 100e3, 0.5e-9)
        p = periods(c, rs, "50us", "4ms", 5)
        sim.check("4046 f at reactStep %s (Hz)" % rs, 1 / statistics.mean(p), 10010, tol=0.001)
        vco(c)
        p = periods(c, rs, "50us", "6ms", 8.5)
        sim.check("566 f at reactStep %s (Hz)" % rs, 1 / statistics.mean(p), 2 * 2 / (10e3 * 10e-9 * 12), tol=0.001)
    pll(c, 5.0, 10e3, 0.833e-9)
    p = periods(c, "40ns", "10us", "600us", 5)
    sim.check("4046 60 kHz on DC: period sd < one 40 ns step (ns)", int(statistics.pstdev(p) * 1e9 < 40), 1, absolute=0)


run_test(test)
