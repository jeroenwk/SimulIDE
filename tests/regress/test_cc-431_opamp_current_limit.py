"""Op amp output current limit, I_Max (create-circuit #431, #430, #415).

The generic op amp sourced any current: in AoE fig 9.2E the crowbar SCR fired, the current-limit
transistor Q2 took the op amp's output into the shorted output (859 A from an ideal op amp),
and V_OUT held 10.23 V with 864 A through the SCR instead of collapsing to about 1 V; the Q2/R_CL
current limit could not clamp either (#430, #415: a real op amp gives up at ~25 mA).

Expected with I_Max = 25 mA (0 = none, the default):
- follower of 5 V (rails +-12 V) into 10 ohm: the output current 25 mA (source), V_OUT 0.25 V,
  state I LIMIT; into 1k: 5 V (5 mA, not limited), state LINEAR; follower of -5 V into 10 ohm:
  -25 mA (sink), -0.25 V;
- without I_Max: 5 V into 10 ohm (0.5 A);
- crowbar: the op amp (I_Max 25 mA) drives an NPN follower (beta 100) from 12 V, set for 5 V
  out, load 10 ohm; an SCR across the output fired by a gate switch: V_OUT falls below 1.5 V,
  and the SCR current stays under beta * 25 mA = 2.5 A plus the load's; the op amp sources 25 mA.
"""
from harness import run_test


def follower(sim, vin, rl, imax):
    c = sim.cli
    c("circuit", "new", "--discard")
    props = ["Volt_Pos=12", "Volt_Neg=-12"] + (["I_Max=%g" % imax] if imax else [])
    c("comp", "add", "opAmp", "--label", "U", "--at", "0,0", *props)
    c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-100,-8", "Voltage=%g" % vin, "Out=true")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "100,40", "--rotate", "90", "Resistance=%g" % rl)
    c("comp", "add", "Ground", "--label", "G", "--at", "100,120")
    c("wire", "add", "VI.outnod", "U.inputNinv")
    c("wire", "add", "U.output", "U.inputInv")
    c("wire", "add", "U.output", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    c("sim", "run", "--for", "1ms")
    p = c("probe", "U.output")[0]
    st = c("comp", "get", "U").get("state")
    c("sim", "stop")
    return p["voltage"], -p["current"], st


def test(sim):
    v, i, st = follower(sim, 5, 10, 0.025)
    sim.check("5 V into 10 ohm, I_Max 25 mA: current (A)", i, 0.025, absolute=1e-4)
    sim.check("5 V into 10 ohm, I_Max 25 mA: V_OUT (V)", v, 0.25, absolute=2e-3)
    sim.check("5 V into 10 ohm: state I LIMIT", 1 if st and "LIMIT" in str(st) else 0, 1, absolute=0, note=str(st))
    v, i, st = follower(sim, 5, 1000, 0.025)
    sim.check("5 V into 1k, I_Max 25 mA: V_OUT (V)", v, 5, absolute=0.01)
    sim.check("5 V into 1k: state LINEAR", 1 if st and "LINEAR" in str(st) else 0, 1, absolute=0, note=str(st))
    v, i, st = follower(sim, -5, 10, 0.025)
    sim.check("-5 V into 10 ohm, I_Max 25 mA: current (A)", i, -0.025, absolute=1e-4)
    sim.check("-5 V into 10 ohm, I_Max 25 mA: V_OUT (V)", v, -0.25, absolute=2e-3)
    v, i, st = follower(sim, 5, 10, 0)
    sim.check("no I_Max: 5 V into 10 ohm (V)", v, 5, absolute=0.01)

    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "opAmp", "--label", "U", "--at", "0,0", "Volt_Pos=12", "Volt_Neg=0", "I_Max=0.025")
    c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-100,-8", "Voltage=5", "Out=true")
    c("comp", "add", "Rail", "--label", "VP", "--at", "100,-100", "Voltage=12")
    c("comp", "add", "BJT", "--label", "Q", "--at", "100,0", "Gain=100")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "200,60", "--rotate", "90", "Resistance=10")
    c("comp", "add", "SCR", "--label", "S", "--at", "300,60", "--rotate", "90")
    c("comp", "add", "Switch", "--label", "SW", "--at", "300,-60")
    c("comp", "add", "Resistor", "--label", "RG", "--at", "380,0", "Resistance=1k")
    c("comp", "add", "Ground", "--label", "G", "--at", "200,160")
    c("wire", "add", "VI.outnod", "U.inputNinv")
    c("wire", "add", "U.output", "Q.base")
    c("wire", "add", "VP.outnod", "Q.collector")
    c("wire", "add", "Q.emiter", "U.inputInv")
    c("wire", "add", "Q.emiter", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    c("wire", "add", "Q.emiter", "S.lPin")
    c("wire", "add", "S.rPin", "G.Gnd")
    c("wire", "add", "VP.outnod", "SW.pinP0")
    c("wire", "add", "SW.switch0pinN", "RG.lPin")
    c("wire", "add", "RG.rPin", "S.gPin")
    c("sim", "run", "--for", "1ms")
    sim.check("crowbar: V_OUT before (V)", c("probe", "Q.emiter")[0]["voltage"], 5, absolute=0.02)
    c("comp", "set", "SW", "Toggled=true")
    c("sim", "run", "--for", "1ms")
    c("comp", "set", "SW", "Toggled=false")
    c("sim", "run", "--for", "2ms")
    vo = c("probe", "Q.emiter")[0]["voltage"]
    isc = c("probe", "S.lPin")[0]["current"]
    iu = -c("probe", "U.output")[0]["current"]
    sim.check("crowbar: V_OUT collapsed (V < 1.5)", 1 if vo < 1.5 else 0, 1, absolute=0, note="%.4g V" % vo)
    sim.check("crowbar: SCR current under 2.6 A", 1 if 0 < isc < 2.6 else 0, 1, absolute=0, note="%.4g A" % isc)
    sim.check("crowbar: op amp sources its limit (A)", iu, 0.025, absolute=1e-3)
    c("sim", "stop")


run_test(test)
