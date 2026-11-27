"""JFET presets inside their datasheet ranges, with capacitances, and a gate breakdown
(create-circuit #622, #628).

The 2N5484/5/6 and 2N5460 entries came from LTspice's Siliconix lines, whose Beta gives
IDSS = Beta*Vto^2 far above the datasheets (2N5485: 14 mA for 4-10 mA; 2N5486: 64 mA for
8-20 mA), so a 24 V / 4.7k Pierce sat in the ohmic region (#622). The same file's J2N lines fall
inside them. Choosing a model left Cgs = Cgd = 0, and the gate had no breakdown: a Pierce gate
swung tens of volts past a 2N5485's -25 V rating (#628).

Expected (datasheet IDSS ranges, onsemi/Siliconix 2N5484-6, 2N5460; LTspice standard.jft):
- 2N5484 1-5 mA, 2N5485 4-10 mA, 2N5486 8-20 mA, 2N5460 1-5 mA;
- 2N5485 model: Cgs 2.414 pF, Cgd 1.6 pF (J2N5485 line);
- BV=25: gate driven to -35 V through 10k (source and drain grounded) conducts about
  (35-25)/10k = 1 mA (the knee within a volt); with BV=0 only picoamps.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def prop(c, label, name):
    for p in c("comp", "get", label)["properties"]:
        if p["name"] == name:
            num, *unit = p["value"].split()
            u = unit[0] if unit else ""
            mul = {"m": 1e-3, "µ": 1e-6, "u": 1e-6, "n": 1e-9, "p": 1e-12}
            return float(num) * (mul.get(u[0], 1) if len(u) > 1 else 1)
    raise KeyError(name)


def within(sim, name, value, lo, hi):
    sim.check("%s = %.4g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Jfet", "--label", "J", "--at", "0,0")
    for model, lo, hi in (("2N5484", 1, 5), ("2N5485", 4, 10), ("2N5486", 8, 20), ("2N5460", 1, 5)):
        c("comp", "set", "J", "Model=" + model)
        within(sim, model + " IDSS mA", prop(c, "J", "IDSS") * 1e3, lo, hi)
    c("comp", "set", "J", "Model=2N5485")
    sim.check("2N5485 Cgs pF", prop(c, "J", "Cgs") * 1e12, 2.414, tol=0, absolute=0.01)
    sim.check("2N5485 Cgd pF", prop(c, "J", "Cgd") * 1e12, 1.6, tol=0, absolute=0.01)

    for bv, lo, hi in ((25, 0.85e-3, 1.0e-3), (0, -1e-9, 1e-9)):
        c("circuit", "new", "--discard")
        c("comp", "add", "Jfet", "--label", "J", "--at", "0,0", "Model=2N5485", "BV=%g" % bv)
        c("comp", "add", "Rail", "--label", "VG", "--at", "-200,0", "Voltage=-35")
        c("comp", "add", "Resistor", "--label", "RG", "--at", "-100,8", "Resistance=10k")
        c("comp", "add", "Ground", "--label", "G", "--at", "40,100")
        c("wire", "add", "VG.outnod", "RG.lPin")
        c("wire", "add", "RG.rPin", "J.Gate")
        c("wire", "add", "J.Sour", "G.Gnd")
        c("wire", "add", "J.Dren", "G.Gnd")
        c("sim", "run", "--for", "1ms")
        i = -c("probe", "RG.lPin")[0]["current"]
        i = abs(i)
        within(sim, "BV=%g: gate current at -35 V through 10k (A)" % bv, i, lo, hi)
        c("sim", "stop")


run_test(test)
