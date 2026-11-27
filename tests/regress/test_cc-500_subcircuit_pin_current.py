"""A subcircuit's pins report the current into it (create-circuit #500).

A package pin has no stamps of its own: the parts inside connect to its node, so
`comp get U1` and `probe U1.Ai` gave 0 A on every pin of a CD4066 and cell currents had to be
worked out by KCL. Expected, a CD4066 cell switching 5 V into 1 k to ground:
- current into Ai = V(Ao)/1k (a few mA), out of Ao the same (KCL through the cell),
- the enable input Ae draws ~0, the resistor's own pin current is unchanged.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "CD4066", "--label", "U1", "--at", "0,0")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-120,0", "Voltage=5", "Out=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "120,80", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "200,120")
    c("wire", "add", "V.outnod", "U1.Ai")
    c("comp", "add", "Fixed Voltage", "--label", "VE", "--at", "-120,80", "Voltage=5", "Out=true")
    c("wire", "add", "VE.outnod", "U1.Ae")   # Its own net: pins on one net share its total
    c("wire", "add", "U1.Ao", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")
    c("sim", "run", "--for", "1ms")
    vo = c("probe", "U1.Ao")[0]["voltage"]
    i = vo/1000
    sim.check("cell output V(Ao) %.4g V > 3 V" % vo, int(vo > 3), 1, absolute=0)
    ai, ao, ae = (c("probe", "U1." + n)[0]["current"] for n in ("Ai", "Ao", "Ae"))
    sim.check("probe: current into Ai", ai, i, tol=0.01)
    sim.check("probe: current into Ao", ao, -i, tol=0.01)
    sim.check("probe: current into Ae (|I| < 1 uA)", int(abs(ae) < 1e-6), 1, absolute=0)
    pins = {p["id"]: p for p in c("comp", "get", "U1")["pins"]}
    sim.check("comp get: current into Ai", pins["Ai"]["current"], i, tol=0.01)
    sim.check("resistor pin current unchanged", c("probe", "R.lPin")[0]["current"], i, tol=0.01)
    c("sim", "stop")


run_test(test)
