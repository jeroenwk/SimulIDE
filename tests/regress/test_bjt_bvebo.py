"""BJT emitter-base reverse breakdown, property BVebo (create-circuit #69).

A transistor's base driven far below its emitter: a real one breaks down at its BVebo
(about 5-6 V for small-signal parts; 2N2222: 6 V) and holds the base there, which is
why astable multivibrators on higher supplies need base diodes. SimulIDE's BJT had no
B-E breakdown: the base went to whatever the drive gave (-9.4 V in a Duncan 56.1 build).

Circuit: NPN, emitter grounded, collector to +5 V through 1 k; base from -12 V through
10 k (PNP: mirrored, +12 V and -5 V).

With BVebo = 6 V (expected, the same knee as BVceo and SimulIDE's zener: 5 mA at BV,
e-fold per Vt): the base current is (12 - 6)/10 k = 0.6 mA, Vt*ln(0.6/5) = -55 mV,
so VBE = -5.945 V; the collector stays off (5 V); the part state reads BREAKDOWN.
BVebo = 0 (default): no breakdown, VBE = -12 V (the old behaviour).
BVebo survives save and reopen.
"""

import os
import tempfile
from harness import run_test


def build(sim, bv=None, pnp=False):
    c = sim.cli
    s = -1 if pnp else 1
    c("circuit", "new", "--discard")
    extra = ["BVebo=%s V" % bv] if bv is not None else []
    if pnp:
        extra.append("PNP=true")
    c("comp", "add", "BJT", "--label", "Q1", "--at", "0,0", *extra)
    c("comp", "add", "Ground", "--label", "G", "--at", "0,120")
    c("comp", "add", "Fixed Voltage", "--label", "VCC", "--at", "-40,-160", "Voltage=%g" % (5 * s), "Out=true")
    c("comp", "add", "Resistor", "--label", "RC", "--at", "8,-80", "--rot", "90", "Resistance=1000")
    c("comp", "add", "Fixed Voltage", "--label", "VB", "--at", "-160,0", "Voltage=%g" % (-12 * s), "Out=true")
    c("comp", "add", "Resistor", "--label", "RB", "--at", "-80,0", "Resistance=10000")
    c("wire", "add", "VCC.outnod", "RC.lPin")
    c("wire", "add", "RC.rPin", "Q1.collector")
    c("wire", "add", "Q1.emiter", "G.Gnd")
    c("wire", "add", "VB.outnod", "RB.lPin")
    c("wire", "add", "RB.rPin", "Q1.base")


def measure(sim, pnp=False):
    s = -1 if pnp else 1
    sim.cli("sim", "run", "--for", "1ms")
    vbe = s * sim.cli("probe", "Q1.base")[0]["voltage"]
    vc = s * sim.cli("probe", "Q1.collector")[0]["voltage"]
    st = sim.cli("read", "Q1").get("state")
    sim.cli("sim", "stop")
    return vbe, vc, st


def test(sim):
    try:
        build(sim, bv=6)
    except Exception as e:                                  # no BVebo property yet
        sim.check("BJT accepts BVebo (%s)" % str(e)[-120:], 0, 1, absolute=0)
        return
    vbe, vc, st = measure(sim)
    sim.check("VBE clamped at -BVebo (V)", vbe, -5.945, absolute=0.03)
    sim.check("collector stays off (V)", vc, 5, absolute=0.05)
    sim.check("state while clamped (%s)" % st, int(st == "BREAKDOWN"), 1, absolute=0)

    build(sim, bv=6, pnp=True)
    vbe, vc, st = measure(sim, pnp=True)
    sim.check("PNP VBE clamped at +BVebo (V, sign flipped)", vbe, -5.945, absolute=0.03)

    build(sim)
    vbe, vc, st = measure(sim)
    sim.check("BVebo 0: no breakdown, VBE follows the drive (V)", vbe, -12, absolute=0.05)
    sim.check("BVebo 0: state OFF (%s)" % st, int(st == "OFF"), 1, absolute=0)

    build(sim, bv=6)
    p = os.path.join(tempfile.mkdtemp(prefix="bjt-bvebo-"), "q.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "Q1")["properties"]}
    sim.check("reopened BVebo (%s)" % props.get("BVebo"), 1 if str(props.get("BVebo", "")).startswith("6") else 0, 1,
              absolute=0)


run_test(test)
