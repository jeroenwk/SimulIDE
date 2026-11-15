"""BJT collector-emitter breakdown, property BVceo (create-circuit #150).

A coil switched off by a transistor with no flyback diode: the coil keeps its current
and drives the collector up until something conducts. A real transistor avalanches at
its BVceo (2N2222: 40 V) and holds the collector there while the coil's energy drains;
SimulIDE's BJT had no breakdown, so the collector jumped to I*L/step (kilovolts) for
one reactive step and the current vanished.

Circuit: 12 V -> 100 mH (100 ohm) -> collector; emitter to ground; base from a 5 V
source through 1 k (Ic = 12/100 = 120 mA; beta 100 needs 1.2 mA, the base gets 4.3 mA:
saturated).
The base battery is set to 0 V, then the collector is sampled every 2 us.

With BVceo = 40 V (expected from the physics):
- the peak collector voltage is 40 V plus the steep part of the avalanche: the
  current is 5 mA at BVceo (like SimulIDE's zener) and rises e-fold per Vt, so
  120 mA needs Vt*ln(120/5) = 82 mV more: 40.08 V;
- the collector is still clamped after 20 us: the current falls by
  (BV - 12 V - I*R)/L ~ 0.16 A/ms, so the 120 mA needs about 0.7 ms, not one step;
- PNP mirrored (-12 V supply, collector at -40 V).
BVceo = 0 (default): no breakdown, the old behaviour: the spike lasts one reactive step,
so 20 us later the collector is back at the 12 V supply.
The part state reads BREAKDOWN while it clamps.
BVceo survives save and reopen.
"""

import os
import tempfile
from harness import run_test


def build(sim, bv=None, pnp=False):
    c = sim.cli
    s = -1 if pnp else 1
    c("circuit", "new", "--discard")
    extra = ["BVceo=%s V" % bv] if bv is not None else []
    if pnp:
        extra.append("PNP=true")
    c("comp", "add", "BJT", "--label", "Q1", "--at", "0,0", *extra)
    c("comp", "add", "Ground", "--label", "G", "--at", "0,120")
    c("comp", "add", "Fixed Voltage", "--label", "VCC", "--at", "-40,-160", "Voltage=%g" % (12 * s), "Out=true")
    c("comp", "add", "Inductor", "--label", "L1", "--at", "8,-80", "--rot", "90",
      "Inductance=100 mH", "Resistance=100 Ω")
    c("comp", "add", "Fixed Voltage", "--label", "VB", "--at", "-160,0", "Voltage=%g" % (5 * s), "Out=true")
    c("comp", "add", "Resistor", "--label", "RB", "--at", "-80,0", "Resistance=1000")
    c("wire", "add", "VCC.outnod", "L1.lPin")
    c("wire", "add", "L1.rPin", "Q1.collector")
    c("wire", "add", "Q1.emiter", "G.Gnd")
    c("wire", "add", "VB.outnod", "RB.lPin")
    c("wire", "add", "RB.rPin", "Q1.base")
    c("settings", "set", "reactStep=100ns")


def spike(sim, pnp=False):
    s = -1 if pnp else 1
    sim.cli("sim", "run", "--for", "5ms")                  # coil current settles (tau = 1 ms)
    on = s * sim.cli("probe", "Q1.collector")[0]["voltage"]
    sim.cli("comp", "set", "VB", "Voltage=0")
    v = []
    for _ in range(15):
        sim.cli("sim", "run", "--for", "2us")
        v.append(s * sim.cli("probe", "Q1.collector")[0]["voltage"])
    sim.state = sim.cli("read", "Q1").get("state")
    sim.cli("sim", "stop")
    return on, v


def test(sim):
    try:
        build(sim, bv=40)
    except Exception as e:                                  # no BVceo property yet
        sim.check("BJT accepts BVceo (%s)" % str(e)[-120:], 0, 1, absolute=0)
        return
    on, v = spike(sim)
    sim.check("saturated before turn-off (Vce, V)", on, 0.1, absolute=0.2)
    sim.check("peak collector at BVceo (V)", max(v), 40.08, absolute=0.05)
    sim.check("still clamped after 20 us (V)", v[9], 40.08, absolute=0.05)
    st = sim.state
    sim.check("state while clamped (%s)" % st, int(st == "BREAKDOWN"), 1, absolute=0)

    build(sim, bv=40, pnp=True)
    on, v = spike(sim, pnp=True)
    sim.check("PNP peak collector at -BVceo (V, sign flipped)", max(v), 40.08, absolute=0.05)
    sim.check("PNP still clamped after 20 us (V)", v[9], 40.08, absolute=0.05)

    build(sim)
    on, v = spike(sim)
    sim.check("BVceo 0: nothing holds the collector, back at 12 V after 20 us (V)", v[9], 12, absolute=0.5)

    build(sim, bv=40)
    p = os.path.join(tempfile.mkdtemp(prefix="bjt-bv-"), "q.sim1")
    sim.cli("circuit", "save", p)
    sim.cli("circuit", "open", p, "--discard")
    props = {q["name"]: q["value"] for q in sim.cli("comp", "get", "Q1")["properties"]}
    sim.check("reopened BVceo (%s)" % props.get("BVceo"), 1 if str(props.get("BVceo", "")).startswith("40") else 0, 1, absolute=0)


run_test(test)
