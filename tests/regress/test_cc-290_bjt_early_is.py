"""BJT Early voltage (VA) and saturation current (IS) settings (create-circuit #290, #304, #70).

The BJT had no Early effect: with VBE held, the collector current was the same at every
VCE (#304: 1.8886 mA at 4.2 V and 8.9 V), so a current-mirror load had no r_o and a diff
amp rested saturated (#290). Its VBE was fixed by IS = 1e-13 A (0.596 V at 1 mA) with no
way to match a datasheet except the obscure Threshold (#70).

Right result, VBE held at 0.65 V by a source, emitter grounded, collector at VCE:
- VA = 0 (default): Ic(10 V) / Ic(2 V) = 1 exactly (existing circuits unchanged);
- VA = 100 V: Ic scales with 1 - VBC/VA = 1 + (VCE - VBE)/VA: ratio 1.0935 / 1.0135 = 1.0789;
- IS = 1e-14 A: Ic = IS exp(VBE/Vt) = 0.82 mA (Vt = 25.865 mV), a tenth of the default;
the same for a PNP with every voltage reversed.
"""
import os, sys, math
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test

VT = 0.025865
VBE = 0.65


def ic(sim, vce, pnp=False, props=()):
    s = -1 if pnp else 1
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "-200,0", "Voltage=%g" % (s * vce), "Out=true")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "VB", "--at", "-200,200", "Voltage=%g" % (s * VBE), "Out=true")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,100")
    sim.cli("comp", "add", "BJT", "--label", "Q", "--at", "0,0", "PNP=%s" % ("true" if pnp else "false"), *props)
    sim.cli("wire", "add", "VC.outnod", "Q.collector")
    sim.cli("wire", "add", "VB.outnod", "Q.base")
    sim.cli("wire", "add", "Q.emiter", "G.Gnd")
    sim.run("1ms")
    return s * sim.read("Q")["i_collector"]


def test(sim):
    for pnp in (False, True):
        kind = "PNP" if pnp else "NPN"
        a, b = ic(sim, 2, pnp), ic(sim, 10, pnp)
        sim.check("%s VA=0: Ic(10 V)/Ic(2 V) (%.6g / %.6g A)" % (kind, b, a), b / a, 1.0, tol=1e-9)
        a, b = ic(sim, 2, pnp, ("VA=100",)), ic(sim, 10, pnp, ("VA=100",))
        want = (1 + (10 - VBE) / 100) / (1 + (2 - VBE) / 100)
        sim.check("%s VA=100: Ic(10 V)/Ic(2 V) (%.6g / %.6g A)" % (kind, b, a), b / a, want, tol=1e-3)
        got = ic(sim, 5, pnp, ("IS=1e-14",))
        want = 1e-14 * math.exp(VBE / VT)
        sim.check("%s IS=1e-14: Ic at VBE 0.65 V" % kind, got, want, tol=0.01)
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "Q")["properties"]}
    sim.check("comp get shows IS (%s)" % props.get("IS"), int("IS" in props), 1, absolute=0)


run_test(test)
