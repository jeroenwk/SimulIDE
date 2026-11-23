"""Op-amp slew rate, input bias / offset current and offset voltage (AoE 4.37, 4.65; book parts).

The op-amp had only Gain, Out_Imped and the rails: no circuit could show integrator drift
from bias current and offset, or a follower that cannot keep up with a fast edge. New
properties SlewRate (V/us), Ib, Ios (nA) and Vos (mV), all 0 by default (the ideal model,
unchanged). Expected values by hand:
- Vos 5 mV on a follower with + grounded: out = 5 mV; non-inverting x101 (100 k / 1 k):
  out = 101 * 5 mV = 0.505 V.
- Ib 100 nA: follower whose + input sees 1 M to ground: out = -Ib * 1 M = -0.1 V;
  inverting stage 1 M / 1 M with + grounded: out = +Ib * 1 M = +0.1 V.
- Ios 20 nA (Ib+ = Ib + Ios/2): follower with 1 M on +: out = -(100 + 10) nA * 1 M = -0.11 V.
- Integrator drift (AoE 4.65): 1 M in, 10 nF feedback, input grounded, Ib 100 nA: the
  bias current comes from the capacitor, dVout/dt = Ib / C = +10 V/s: +0.1 V after 10 ms;
  with Vos 1 mV as well: +1 mV at once (the follower-like start) and Vos/(R C) = 0.1 V/s: +0.102 V.
- Slew rate 0.5 V/us (741), +-15 V, follower, input step 0 -> 10 V: out 2 V after 4 us,
  10 V after 20 us (and not before 19 us); back to 0 the same way.
- Comparator (open loop, gain 1e5), SR 13 V/us, inputs swapped: -15 -> +15 V in 30/13 = 2.3 us.
- Defaults leave a circuit as it was: a follower follows a 100 kHz sine exactly.
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def amp(sim, props, plus=None, minus="out", rin=None, rf=None, cf=None, rplus=None):
    """Op-amp U (power pins off, rails +-15 V). plus: 'gnd', 'VI' (a fixed source 'VI'),
    minus: 'out' (follower) or a feedback network rin (to ground) / rf / cf."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "OpAmp", "--label", "U", "--at", "0,0", "Volt_Pos=15", "Volt_Neg=-15", "Gain=100000", *props)
    c("comp", "add", "Ground", "--label", "G", "--at", "-120,160")
    if plus == "VI":
        c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-200,-80", "Voltage=10", "Out=false")
        c("wire", "add", "VI.outnod", "U.inputNinv")
    elif rplus:
        c("comp", "add", "Resistor", "--label", "RP", "--at", "-80,40", "--rot", "90", "Resistance=%g" % rplus)
        c("wire", "add", "RP.lPin", "U.inputNinv")
        c("wire", "add", "RP.rPin", "G.Gnd")
    else:
        c("wire", "add", "U.inputNinv", "G.Gnd")
    if minus == "out":
        c("wire", "add", "U.output", "U.inputInv")
        return
    if rin:
        c("comp", "add", "Resistor", "--label", "RI", "--at", "-120,80", "Resistance=%g" % rin)
        c("wire", "add", "RI.rPin", "U.inputInv")
        c("wire", "add", "RI.lPin", "G.Gnd")
    if rf:
        c("comp", "add", "Resistor", "--label", "RF", "--at", "0,-80", "Resistance=%g" % rf)
        c("wire", "add", "U.inputInv", "RF.lPin")
        c("wire", "add", "RF.rPin", "U.output")
    if cf:
        c("comp", "add", "Capacitor", "--label", "CF", "--at", "0,-140", "Capacitance=%g" % cf)
        c("wire", "add", "U.inputInv", "CF.lPin")
        c("wire", "add", "CF.rPin", "U.output")


def out_after(sim, t):
    sim.cli("sim", "run", "--for", t)
    v = sim.cli("probe", "U.output")[0]["voltage"]
    sim.cli("sim", "stop")
    return v


def test(sim):
    c = sim.cli
    try:
        amp(sim, ["Vos=5 mV"])
    except Exception as e:
        sim.check("op amp Vos property (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    within(sim, "Vos 5 mV, follower (mV)", out_after(sim, "1ms") * 1e3, 4.9, 5.1)
    amp(sim, ["Vos=5 mV"], minus=None, rin=1e3, rf=1e5)
    within(sim, "Vos 5 mV, x101 (V)", out_after(sim, "1ms"), 0.50, 0.51)

    amp(sim, ["Ib=100 nA"], rplus=1e6)
    within(sim, "Ib 100 nA, 1 M on + (V)", out_after(sim, "1ms"), -0.101, -0.099)
    amp(sim, ["Ib=100 nA"], minus=None, rin=1e6, rf=1e6)
    within(sim, "Ib 100 nA, inverting 1 M / 1 M (V)", out_after(sim, "1ms"), 0.099, 0.101)
    amp(sim, ["Ib=100 nA", "Ios=20 nA"], rplus=1e6)
    within(sim, "Ib 100 nA + Ios 20 nA, 1 M on + (V)", out_after(sim, "1ms"), -0.111, -0.109)

    amp(sim, ["Ib=100 nA"], minus=None, rin=1e6, cf=10e-9)
    within(sim, "integrator drift, Ib 100 nA, 10 ms (V)", out_after(sim, "10ms"), 0.095, 0.105)
    amp(sim, ["Ib=100 nA", "Vos=1 mV"], minus=None, rin=1e6, cf=10e-9)
    within(sim, "integrator drift, Ib + Vos, 10 ms (V)", out_after(sim, "10ms"), 0.096, 0.106)

    # Slew: follower, step 0 -> 10 V
    amp(sim, ["SlewRate=0.5"], plus="VI")
    c("sim", "run", "--for", "100us")
    c("comp", "set", "VI", "Out=true")
    c("sim", "run", "--for", "4us")
    v4 = c("probe", "U.output")[0]["voltage"]
    c("sim", "run", "--for", "14.5us")
    v185 = c("probe", "U.output")[0]["voltage"]
    c("sim", "run", "--for", "5.5us")
    v24 = c("probe", "U.output")[0]["voltage"]
    c("sim", "run", "--for", "50us")
    c("comp", "set", "VI", "Out=false")
    c("sim", "run", "--for", "10us")
    vdown = c("probe", "U.output")[0]["voltage"]
    c("sim", "stop")
    within(sim, "SR 0.5 V/us: out 4 us after the step (V)", v4, 1.8, 2.2)
    within(sim, "SR 0.5 V/us: out at 18.5 us, still slewing (V)", v185, 8.9, 9.6)
    within(sim, "SR 0.5 V/us: out at 24 us, settled (V)", v24, 9.99, 10.01)
    within(sim, "SR 0.5 V/us: 10 us after the step down (V)", vdown, 4.8, 5.2)

    # Comparator: - at 1 V, + grounded, then - to -1 V
    c("circuit", "new", "--discard")
    c("comp", "add", "OpAmp", "--label", "U", "--at", "0,0", "Volt_Pos=15", "Volt_Neg=-15", "Gain=100000",
      "SlewRate=13")
    c("comp", "add", "Ground", "--label", "G", "--at", "-120,160")
    c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-200,-80", "Voltage=1", "Out=true")
    c("wire", "add", "VI.outnod", "U.inputInv")
    c("wire", "add", "U.inputNinv", "G.Gnd")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "80,60", "--rot", "90", "Resistance=10000")
    c("wire", "add", "U.output", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    c("sim", "run", "--for", "100us")
    v0 = c("probe", "U.output")[0]["voltage"]
    c("comp", "set", "VI", "Voltage=-1")          # - below +: the output swings up
    c("sim", "run", "--for", "1.15us")
    vmid = c("probe", "U.output")[0]["voltage"]
    c("sim", "run", "--for", "2us")
    vend = c("probe", "U.output")[0]["voltage"]
    c("sim", "stop")
    within(sim, "comparator before (V)", v0, -15.01, -14.99)
    within(sim, "comparator SR 13 V/us, 1.15 us into the swing (V)", vmid, -2, 2)
    within(sim, "comparator after 3.15 us (V)", vend, 14.99, 15.01)

    # Default: no slew, a follower follows a 100 kHz, 10 V sine
    c("circuit", "new", "--discard")
    c("comp", "add", "OpAmp", "--label", "U", "--at", "0,0", "Volt_Pos=15", "Volt_Neg=-15", "Gain=100000")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,-80", "Wave_Type=Sine", "Freq=100 kHz",
      "Mid_Volt=0", "Semi_Ampli=10", "Always_On=true")
    c("wire", "add", "W.outnod", "U.inputNinv")
    c("wire", "add", "U.output", "U.inputInv")
    worst = 0
    c("sim", "run", "--for", "20us")
    for i in range(20):
        c("sim", "run", "--for", "0.37us")
        worst = max(worst, abs(c("probe", "U.output")[0]["voltage"] - c("probe", "W.outnod")[0]["voltage"]))
    c("sim", "stop")
    within(sim, "no slew rate: follower error at 100 kHz (mV)", worst * 1e3, 0, 1)

    c("circuit", "new", "--discard")
    c("comp", "add", "OpAmp", "--label", "U", "--at", "0,0", "Volt_Pos=15", "Volt_Neg=-15", "Gain=100000",
      "SlewRate=0.5")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,-80", "Wave_Type=Sine", "Freq=100 kHz",
      "Mid_Volt=0", "Semi_Ampli=10", "Always_On=true")
    c("wire", "add", "W.outnod", "U.inputNinv")
    c("wire", "add", "U.output", "U.inputInv")
    c("sim", "run", "--for", "100us")
    lo, hi = 99, -99
    for i in range(40):
        c("sim", "run", "--for", "0.25us")
        v = c("probe", "U.output")[0]["voltage"]
        lo, hi = min(lo, v), max(hi, v)
    c("sim", "stop")
    # A triangle at 0.5 V/us: 5 us per half period -> 2.5 V peak-to-peak at most
    within(sim, "SR 0.5 V/us at 100 kHz, 10 V peak: output p-p (V)", hi - lo, 1.5, 2.6)


run_test(test)
