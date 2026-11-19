"""Varistor: the datasheet power law I = 1 mA (|V|/V1mA)^alpha, and its states.

A metal-oxide varistor's current rises as a power of its voltage. Through the two datasheet
points of a 07D561K (1 mA at 560 V, at most 925 V at 10 A): alpha = ln(10/1e-3)/ln(925/560) =
18.35. The old model (two zener junctions in anti-series plus a resistor) drew a straight line
from the 560 V knee to the datasheet point instead: 3.8 A at 700 V, where the power law gives
1e-3 * (700/560)^18.35 = 60.1 mA. Plus a 10 GOhm leak (10 nA per 100 V).

Right results:
- read Z1 gives alpha 18.35, and at 325 / 560 / 700 / 925 V the current of the power law
  (plus V/10 GOhm), within 0.5 %; the same mirrored with the varistor turned round;
- 1000 V through 10 Ohm clamps where (1000 - V)/10 = I(V): 916.2 V, 8.38 A (bisection below);
- states: IDLE at 325 V (78 nA), CLAMPING at 700 V, OVER at 925 V with MaxCurrent 5 A;
- MaxCurrent survives save and reopen.
"""
import math
import os
import tempfile
from harness import run_test

V1MA, VCL, ICL, GLEAK = 560.0, 925.0, 10.0, 1e-10
ALPHA = math.log(ICL / 1e-3) / math.log(VCL / V1MA)


def i_of(v):
    return math.copysign(1e-3 * (abs(v) / V1MA) ** ALPHA, v) + GLEAK * v


def clamp(vs, r):
    lo, hi = 0.0, vs
    for _ in range(200):
        m = (lo + hi) / 2
        if (vs - m) / r > i_of(m):
            lo = m
        else:
            hi = m
    return lo


def build(sim, r=None, rot="90"):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=325")
    c("comp", "add", "Varistor", "--label", "Z1", "--at", "96,0", "--rot", rot)
    c("comp", "add", "Ground", "--label", "G", "--at", "0,96")
    if r:
        c("comp", "add", "Resistor", "--label", "R1", "--at", "48,-48", "Resistance=" + r)
        c("wire", "add", "B1.rPin", "R1.lPin"); c("wire", "add", "R1.rPin", "Z1.lPin")
    else:
        c("wire", "add", "B1.rPin", "Z1.lPin")
    c("wire", "add", "Z1.rPin", "G.Gnd"); c("wire", "add", "B1.lPin", "G.Gnd")


def at(sim, volt):
    sim.cli("sim", "stop")
    sim.cli("comp", "set", "B1", "Voltage=%g" % volt)
    sim.run("1ms")
    return sim.read("Z1")


def test(sim):
    build(sim)
    sim.run("1ms")
    sim.check("alpha of 07D561K (560 V; 925 V at 10 A)", sim.read("Z1")["alpha"], ALPHA, tol=1e-3)
    for v in (325, 560, 700, 925):            # Z1 drawn with lPin to ground: reads -V, -I
        z = at(sim, v)
        sim.check("I at %g V (A)" % v, z["current"], i_of(z["voltage"]), tol=0.005,
                  note="power law at the solved voltage %.4g V" % z["voltage"])
        sim.check("|solved voltage| %g V" % v, abs(z["voltage"]), v, tol=1e-3)
    build(sim, rot="-90")                      # The other way round: the same, mirrored
    z = at(sim, 700)
    sim.check("reversed, 700 V: I (A)", z["current"], i_of(z["voltage"]), tol=0.005)
    sim.check("reversed: |I| as forward (A)", abs(z["current"]), i_of(700), tol=0.005)
    build(sim)
    z = at(sim, 325)
    sim.check("state at 325 V: IDLE", int(z.get("state") == "IDLE"), 1, absolute=0, note=str(z.get("state")))
    z = at(sim, 700)
    sim.check("state at 700 V: CLAMPING", int(z.get("state") == "CLAMPING"), 1, absolute=0, note=str(z.get("state")))
    sim.cli("comp", "set", "Z1", "MaxCurrent=5")
    z = at(sim, 925)
    sim.check("state at 925 V (10 A > 5 A): OVER", int(z.get("state") == "OVER"), 1, absolute=0, note=str(z.get("state")))
    sim.cli("sim", "stop")

    path = os.path.join(tempfile.mkdtemp(prefix="varistor-"), "z1.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("circuit", "open", path, "--discard")
    props = {p["name"]: p["value"] for p in sim.cli("comp", "get", "Z1")["properties"]}
    sim.check("MaxCurrent after save + open (A)", float(props["MaxCurrent"].split()[0]), 5, tol=1e-6)

    build(sim, r="10")                         # 1000 V through 10 Ohm: a surge clamped
    vc = clamp(1000, 10)
    z = at(sim, 1000)
    sim.check("1000 V via 10 Ohm: clamps at (V)", abs(z["voltage"]), vc, tol=1e-3)
    sim.check("... current (A)", abs(z["current"]), (1000 - vc) / 10, tol=5e-3)
    bad = [l for l in sim.cli("messages")["lines"] if "Not converging" in l]
    sim.check("no non-convergence", len(bad), 0, absolute=0)
    sim.finite("varistor", "Z1")


if __name__ == "__main__":
    run_test(test)
