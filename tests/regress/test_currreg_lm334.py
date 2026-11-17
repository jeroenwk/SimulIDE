"""Current Source IC (CurrReg), model LM334: checks against TI's datasheet SNVS746E (2013).

Not a bug: the feature test of the part (docs/CURRENT_SOURCE_IC.md). Expected values are the
datasheet's:
- I_SET = 67.7 mV / R_SET at 25 °C, V+ = 2.5 V (p.3 note 2), for R_SET 14 Ω .. 6.8 kΩ. The
  model holds 64 mV across R_SET and adds the bias current from the Fig 12 ratio curve, so it
  lands within ~2 % of the formula (the ratio is 17-18 between 100 µA and 1 mA, 13.5 at 5 mA).
- I_SET / I_BIAS 14..26, typically 18 (p.3), for 100 µA .. 1 mA.
- Line regulation 0.02 %/V (1.5-5 V) and 0.01 %/V (5-40 V) typical (p.3).
- Turn-on (p.3 minimum operating voltage, p.5 Fig 11): working at 0.8 V for 100 µA; about 1 µA
  at 0.5 V.
- I_SET proportional to absolute temperature (p.3 note 3).
- Reverse 20 V draws "only a few dozen microamperes" (p.1): the model takes 30 µA [INFERRED].
- True 2-terminal: the same current with the load on the high side.
"""

from harness import run_test


def build(sim, rset, vin):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", f"Voltage={vin}")
    sim.cli("comp", "add", "CurrReg", "--label", "U1", "--at", "80,0")
    sim.cli("comp", "add", "Resistor", "--label", "RS", "--at", "160,40", f"Resistance={rset}")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,120")
    sim.cli("wire", "add", "B1.lPin", "U1.vp")
    sim.cli("wire", "add", "U1.r", "RS.lPin")
    sim.cli("wire", "add", "RS.rPin", "G.Gnd")
    sim.cli("wire", "add", "U1.vm", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")


def iset(sim):
    sim.run("1ms")
    return sim.read("U1")


def test(sim):
    # Set current, 25 °C, V+ = 2.5 V: 67.7 mV / R_SET
    for rset, tol in [(6800, 0.03), (680, 0.02), (68, 0.02), (14, 0.03)]:
        build(sim, rset, 2.5)
        r = iset(sim)
        sim.check(f"I_SET R_SET {rset} ohm", r["i_set"], 0.0677/rset, tol=tol)
        sim.check(f"V_R R_SET {rset} ohm (64 mV)", r["v_r"], 0.064, tol=0.005)
        if rset in (680, 68):
            sim.check(f"I_SET/I_BIAS at R_SET {rset} (18 typ, 14..26)", r["i_set"]/r["i_bias"], 18, tol=0.06)
            sim.check(f"state at R_SET {rset}", 1 if r.get("state") == "REG" else 0, 1, absolute=0)

    # Line regulation, R_SET 680 (100 µA)
    build(sim, 680, 1.5); i15 = iset(sim)["i_set"]
    sim.set("B1", Voltage=5); i5 = iset(sim)["i_set"]
    sim.set("B1", Voltage=40); i40 = iset(sim)["i_set"]
    sim.check("line reg 1.5-5 V (%/V)", (i5/i15-1)*100/3.5, 0.02, tol=0.1)
    sim.check("line reg 5-40 V (%/V)", (i40/i5-1)*100/35, 0.01, tol=0.1)

    # Turn-on: 100 µA (680 Ω) working at 0.8 V, about 1 µA at 0.5 V
    sim.set("B1", Voltage=0.8); r = iset(sim)
    sim.check("I at 0.8 V, 680 ohm (>= 94 % of 99.6 uA)", r["i_set"], 0.97*0.0677/680, tol=0.03)
    sim.set("B1", Voltage=0.5); r = iset(sim)
    sim.check("I at 0.5 V, 680 ohm (Fig 11: ~1 uA)", r["i_set"], 0.5e-6, absolute=0.6e-6)
    sim.check("state at 0.5 V", 1 if r.get("state") == "LOW V" else 0, 1, absolute=0)

    # Temperature: proportional to T (K)
    sim.set("B1", Voltage=2.5); i25 = iset(sim)["i_set"]
    sim.set("U1", Temp=75); i75 = iset(sim)["i_set"]
    sim.check("I_SET(75 C)/I_SET(25 C) = 348.15/298.15", i75/i25, 348.15/298.15, tol=0.002)

    # Reverse 20 V (battery turned round: it takes no negative voltage): a few dozen µA
    sim.set("U1", Temp=25)
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=20")
    sim.cli("comp", "add", "CurrReg", "--label", "U1", "--at", "80,0")
    sim.cli("comp", "add", "Resistor", "--label", "RS", "--at", "160,40", "Resistance=680")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,120")
    sim.cli("wire", "add", "B1.rPin", "U1.vp")
    sim.cli("wire", "add", "U1.r", "RS.lPin")
    sim.cli("wire", "add", "RS.rPin", "G.Gnd")
    sim.cli("wire", "add", "U1.vm", "G.Gnd")
    sim.cli("wire", "add", "B1.lPin", "G.Gnd")
    r = iset(sim)
    sim.check("reverse 20 V: headroom", r["voltage"], -20, tol=0.001)
    sim.check("reverse 20 V current (30 uA)", r["i_set"], -30e-6, tol=0.05)
    sim.check("state reversed", 1 if r.get("state") == "OFF" else 0, 1, absolute=0)

    # Floating 2-terminal: 4.7 k load between the supply and V+, 15 V, 1 mA (R_SET 68 Ω)
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=15")
    sim.cli("comp", "add", "Resistor", "--label", "RL", "--at", "40,-40", "Resistance=4700")
    sim.cli("comp", "add", "CurrReg", "--label", "U1", "--at", "120,0")
    sim.cli("comp", "add", "Resistor", "--label", "RS", "--at", "200,40", "Resistance=68")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,120")
    sim.cli("wire", "add", "B1.lPin", "RL.lPin")
    sim.cli("wire", "add", "RL.rPin", "U1.vp")
    sim.cli("wire", "add", "U1.r", "RS.lPin")
    sim.cli("wire", "add", "RS.rPin", "U1.vm")
    sim.cli("wire", "add", "U1.vm", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    sim.run("1ms")
    rl = sim.read("RL")
    sim.check("load current, floating (67.7 mV/68)", abs(rl["current"]), 0.0677/68, tol=0.02)
    sim.finite("U1 pins", "U1")

    # Compliance: the load takes the headroom away. Each value from a regulating start and
    # back, so the result does not depend on the path (it once jumped OFF <-> 7 A)
    # Fig 11 (68 ohm curve) read at the headroom each load leaves: 1.0 V 0.99, 0.9 V 0.97, 0.78 V 0.70
    expect = {10000: 0.995, 13500: 0.99, 14000: 0.98, 14500: 0.95, 15000: 0.95, 20000: 0.70}
    for rl_ohm, frac in expect.items():
        sim.set("RL", Resistance=4700); iset(sim)
        sim.set("RL", Resistance=rl_ohm); a = iset(sim)["i_set"]
        sim.set("RL", Resistance=rl_ohm*1.0001); b = iset(sim)["i_set"]
        sim.check(f"RL {rl_ohm}: I_SET >= {frac:.0%} of 1 mA (Fig 11)", a/0.000996, frac, tol=0.08)
        sim.check(f"RL {rl_ohm}: same after a tiny change", b/a, 1, tol=0.001)

    # Ramp: 1 mA into 1 uF from 12 V, the sawtooth oscillator's charging current (AoE fig 7.12)
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=12")
    sim.cli("comp", "add", "CurrReg", "--label", "U1", "--at", "80,0")
    sim.cli("comp", "add", "Resistor", "--label", "RS", "--at", "160,40", "Resistance=68")
    sim.cli("comp", "add", "Capacitor", "--label", "C1", "--at", "80,80", "Capacitance=1e-6")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,160")
    sim.cli("wire", "add", "B1.lPin", "U1.vp")
    sim.cli("wire", "add", "U1.r", "RS.lPin")
    sim.cli("wire", "add", "RS.rPin", "U1.vm")
    sim.cli("wire", "add", "U1.vm", "C1.lPin")
    sim.cli("wire", "add", "C1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    sim.run("5ms")
    v5 = sim.read("C1")["voltage"]
    sim.check("ramp: 5 ms x 1 mA / 1 uF = 5 V", abs(v5), 5*0.0677/68/1e-3, tol=0.02)
    sim.run("10ms")
    r = sim.read("U1")
    sim.check("ramp end: V+ - V- at the compliance edge (Fig 11: < 1 V)", r["voltage"], 0.6, absolute=0.3)
    msgs = str(sim.cli("messages"))
    sim.check("no solver non-convergence", 1 if "Not converging" not in msgs else 0, 1, absolute=0)

if __name__ == "__main__":
    run_test(test)
