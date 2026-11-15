"""BJT: the convergence aid gmin stayed in the matrix after a step converged.

eBJT::voltChanged adds a conductance gmin across both junctions that grows with each
iteration of a step (up to 0.1 S) to help Newton converge. When the step then
converged, the stamps were kept, gmin included, and nothing restamped them while the
voltages stayed within 1 mV. After a hard switching edge (hundreds of iterations) a
junction could stay shunted by ~10-20 ohm: a two-transistor latch (PUT / UJT model)
then sat in a state that is no solution of the circuit (8.4 V on the emitter with
76 uA flowing in, the NPN held off), and the relaxation oscillator stopped for good
after 30-80 ms.

Circuit: the UJT equivalent from discrete parts. PNP Q1 (gain 100): emitter = E,
base = gate, collector = mid; NPN Q2 (gain 3.5): collector = gate, base = mid,
emitter = ground; 1 M from mid to ground; gate divider 2.45 k from 12 V, 4.55 k to
ground (eta 0.65, RBB 7 k); 47 k from 12 V and 100 nF on E.
Expected: it keeps oscillating, f = 1 / (R C ln((12 - VV) / (12 - VP))) with
VP = 0.65*12 + 0.45 V, VV = 0.7 V: 193 Hz, within 10 %, read after 300 ms.
"""

import math
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-240,-200", "Voltage=12", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,240")
    c("comp", "add", "Resistor", "--label", "RT", "--at", "-160,-80", "--rot", "90", "Resistance=47000")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "-160,80", "--rot", "90", "Capacitance=100 nF")
    c("comp", "add", "Frequency Meter", "--label", "F", "--at", "-300,40")
    c("comp", "add", "BJT", "--label", "Q1", "--at", "-40,0", "PNP=true", "Gain=100")
    c("comp", "add", "BJT", "--label", "Q2", "--at", "80,80", "Gain=3.5")
    c("comp", "add", "Resistor", "--label", "RSH", "--at", "40,160", "--rot", "90", "Resistance=1000000")
    c("comp", "add", "Resistor", "--label", "RB2", "--at", "120,-120", "--rot", "90", "Resistance=2450")
    c("comp", "add", "Resistor", "--label", "RB1", "--at", "200,80", "--rot", "90", "Resistance=4550")
    for a, b in (("V.outnod", "RT.lPin"), ("RT.rPin", "Q1.emiter"), ("C1.lPin", "Q1.emiter"),
                 ("F.lPin", "Q1.emiter"), ("C1.rPin", "G.Gnd"),
                 ("Q1.collector", "Q2.base"), ("RSH.lPin", "Q2.base"), ("RSH.rPin", "G.Gnd"),
                 ("Q2.emiter", "G.Gnd"), ("Q2.collector", "Q1.base"),
                 ("V.outnod", "RB2.lPin"), ("RB2.rPin", "Q1.base"), ("RB1.lPin", "Q1.base"), ("RB1.rPin", "G.Gnd")):
        c("wire", "add", a, b)
    c("sim", "run", "--for", "300ms", timeout=300)
    f = sim.cli("read", "F").get("value") or 0
    v = sim.cli("probe", "Q1.emiter")[0]["voltage"]
    sim.cli("sim", "stop")
    want = 1 / (47e3 * 100e-9 * math.log((12 - 0.7) / (12 - (0.65 * 12 + 0.45))))
    sim.check("still oscillating after 300 ms (Hz; emitter at %.3f V)" % v, float(f), want, tol=0.10)


run_test(test)
