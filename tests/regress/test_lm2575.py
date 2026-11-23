"""LM2575 step-down regulator against its datasheet (Scherz Fig. 11.22).

TI LM2575 (SLVS569F, 2015), Electrical Characteristics p. 4-5, with the datasheet's
application circuit: 100 uF in, 330 uH, a diode, 330 uF out (reactStep 200 ns):
- Output 5 V (fixed model; feedback 1.23 V +-1.2 % typ range -> 4.94..5.06 V) at 200 mA and
  1 A, and over VIN 8..40 V: within 2 %.
- Oscillator 52 kHz typ (47..58 kHz).
- Efficiency 77 % typ at VIN 12 V, VOUT 5 V, 1 A: 70..85 % (the model's diode and switch
  are its only losses besides the quiescent current).
- Peak current limit 2.8 A typ (1.7..3.6 A): a 1 Ohm load is cut cycle by cycle, its current
  stays within the limits.
- ON/OFF high: switch off, standby current 50 uA typ.
- ADJ model: VOUT = 1.23 (1 + R2/R1): 1 k / 3 k -> 4.92 V; 12 V model from 20 V: 12 V.
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def buck(sim, vin=12, rl=25, model="5.0", onoff=False, adj=None):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("settings", "set", "reactStep=200ns")
    c("comp", "add", "LM2575", "--label", "U", "--at", "0,0", "Model=%s" % model)
    c("comp", "add", "Fixed Voltage", "--label", "VI", "--at", "-120,-80", "Voltage=%g" % vin, "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,120")
    c("comp", "add", "Capacitor", "--label", "CI", "--at", "-80,40", "--rot", "90", "Capacitance=100 µF")
    c("comp", "add", "Diode", "--label", "D", "--at", "60,60", "--rot", "-90")
    c("comp", "add", "Inductor", "--label", "L", "--at", "100,0", "Inductance=330 µH")
    c("comp", "add", "Capacitor", "--label", "C", "--at", "160,60", "--rot", "90", "Capacitance=330 µF")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "200,60", "--rot", "90", "Resistance=%g" % rl)
    c("comp", "add", "Frequency Meter", "--label", "F", "--at", "-100,80")
    for a, b in [("VI.outnod", "U.vin"), ("VI.outnod", "CI.lPin"), ("CI.rPin", "G.Gnd"), ("U.gnd", "G.Gnd"),
                 ("U.out", "L.lPin"), ("U.out", "D.rPin"), ("D.lPin", "G.Gnd"), ("L.rPin", "C.lPin"),
                 ("C.rPin", "G.Gnd"), ("L.rPin", "RL.lPin"), ("RL.rPin", "G.Gnd"), ("F.lPin", "U.out")]:
        c("wire", "add", a, b)
    if onoff:
        c("wire", "add", "VI.outnod", "U.onoff")
    else:
        c("wire", "add", "U.onoff", "G.Gnd")
    if adj:
        c("comp", "add", "Resistor", "--label", "R2", "--at", "240,0", "--rot", "90", "Resistance=%g" % adj[1])
        c("comp", "add", "Resistor", "--label", "R1", "--at", "240,80", "--rot", "90", "Resistance=%g" % adj[0])
        for a, b in [("L.rPin", "R2.lPin"), ("R2.rPin", "R1.lPin"), ("R1.rPin", "G.Gnd"), ("R2.rPin", "U.fb")]:
            c("wire", "add", a, b)
    else:
        c("wire", "add", "L.rPin", "U.fb")


def run(sim, t="30ms"):
    sim.cli("sim", "run", "--for", t, timeout=900)
    vout, iin = 0, 0
    for i in range(50):                                # Means over two switching cycles
        sim.cli("sim", "run", "--for", "0.77us")
        vout += sim.cli("probe", "C.lPin")[0]["voltage"] / 50
        iin += abs(sim.cli("probe", "VI.outnod")[0]["current"]) / 50
    return vout, iin


def test(sim):
    c = sim.cli
    try:
        buck(sim)
    except Exception as e:
        sim.check("LM2575 part exists (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    v, i = run(sim)
    within(sim, "VOUT, 12 V in, 200 mA (V)", v, 4.9, 5.1)
    within(sim, "oscillator (kHz)", (c("read", "F")["value"] or 0) / 1e3, 47, 58)
    c("sim", "stop")

    buck(sim, rl=5)
    v, i = run(sim)
    within(sim, "VOUT, 12 V in, 1 A (V)", v, 4.9, 5.1)
    within(sim, "efficiency at 1 A (%)", 100 * v * v / 5 / (12 * i), 70, 85)
    c("sim", "stop")

    for vin in (8, 40):
        buck(sim, vin=vin, rl=25)
        v, i = run(sim)
        within(sim, "VOUT, %g V in (V)" % vin, v, 4.9, 5.1)
        c("sim", "stop")

    buck(sim, rl=1)
    v, i = run(sim)
    lim = c("read", "U")["limited"]
    c("sim", "stop")
    within(sim, "1 ohm load: output current (A)", v / 1, 1.7, 3.6)
    sim.check("1 ohm load: cycles cut by the current limit (%d)" % lim, int(lim > 0), 1, absolute=0)

    buck(sim, onoff=True)
    v, i = run(sim, "10ms")
    c("sim", "stop")
    within(sim, "ON/OFF high: VOUT (V)", v, 0, 0.01)
    within(sim, "ON/OFF high: standby current (uA)", i * 1e6, 25, 100)

    buck(sim, model="ADJ", adj=(1e3, 3e3))
    v, i = run(sim)
    c("sim", "stop")
    within(sim, "ADJ, 1 k / 3 k: VOUT (V), 4.92 by the formula", v, 4.82, 5.02)

    buck(sim, vin=20, model="12")
    v, i = run(sim)
    c("sim", "stop")
    within(sim, "12 V model from 20 V (V)", v, 11.76, 12.24)


run_test(test)
