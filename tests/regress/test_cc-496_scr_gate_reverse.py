"""An SCR's gate-cathode junction blocks in reverse (create-circuit #496).

The gate was a 100 ohm resistor to the cathode, so a negative gate drive pulled the cathode
and its load below ground (a 12 V peak source through 100 ohm put -4 V on a 100 ohm load).
A real gate-cathode is a PN junction: reverse-biased it carries no current.
Expected by hand, anode open (blocked), cathode to ground through RL 100 ohm:
- gate at -5 V through 100 ohm: cathode ~0 V (|V| < 1 mV), no gate current;
- gate at +5 V through 1 k: about (5 - 0.7) / 1.1 k = 3.9 mA forward, cathode about 0.39 V;
- with 12 V on the anode through 100 ohm, +5 V through 100 ohm (about 21 mA, above the
  10 mA TrigCurr) still fires it: the anode load drops most of the 12 V (load current > 50 mA).
"""

from harness import run_test


def build(c, vg, rg, anode):
    c("circuit", "new", "--discard")
    c("comp", "add", "SCR", "--label", "S", "--at", "0,0")
    c("comp", "add", "Fixed Voltage", "--label", "VG", "--at", "-100,80", "Voltage=%g" % vg, "Out=true")
    c("comp", "add", "Resistor", "--label", "RG", "--at", "-40,80", "Resistance=%g" % rg)
    c("comp", "add", "Resistor", "--label", "RL", "--at", "80,40", "Resistance=100")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,120")
    c("wire", "add", "VG.outnod", "RG.lPin")
    c("wire", "add", "RG.rPin", "S.gPin")
    c("wire", "add", "S.rPin", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    if anode:
        c("comp", "add", "Fixed Voltage", "--label", "VA", "--at", "-100,-60", "Voltage=12", "Out=true")
        c("comp", "add", "Resistor", "--label", "RA", "--at", "-50,-60", "Resistance=100")
        c("wire", "add", "VA.outnod", "RA.lPin")
        c("wire", "add", "RA.rPin", "S.lPin")


def test(sim):
    c = sim.cli
    build(c, -5, 100, False)
    c("sim", "run", "--for", "1ms")
    vk = c("probe", "S.rPin")[0]["voltage"]
    sim.check("gate -5 V: cathode voltage (V)", vk, 0.0, absolute=1e-3)
    c("sim", "stop")
    build(c, 5, 1000, False)
    c("sim", "run", "--for", "1ms")
    vk = c("probe", "S.rPin")[0]["voltage"]
    sim.check("gate +5 V through 1 k: cathode voltage (V)", vk, 0.39, tol=0.15)
    c("sim", "stop")
    build(c, 5, 100, True)
    c("sim", "run", "--for", "1ms")
    ia = (12 - c("probe", "S.lPin")[0]["voltage"]) / 100
    sim.check("fired: anode current > 50 mA (%.4g A)" % ia, int(ia > 0.05), 1, absolute=0)


run_test(test)
