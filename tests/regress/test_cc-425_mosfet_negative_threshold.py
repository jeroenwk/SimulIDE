"""A negative Mosfet Threshold is a depletion part's pinch-off (create-circuit #425, #427).

setThreshold returned for any value below 0.01 V, so `Threshold=-2V` was dropped and the
part kept 3 V (an enhancement FET): a depletion part (LND150, pinch-off -1..-3 V) could
only be tuned through Depletion=true and a positive Threshold T, which turns off at
-1.25 T.

Right result: Threshold=-2V is stored, and the n-channel part conducts at V_GS = -1 V
(above pinch-off) and is off at V_GS = -3 V (below it, the definition of pinch-off).
Circuit: gate grounded, the source lifted by a battery VS (V_GS = -VS), drain fed from
12 V through 1 kOhm. Off: the drain resistor carries ~0 A. On: the channel at
V_GS - V_th = 1 V of overdrive carries well over 100 uA (the exact value is the model's
square law, not checked here).
A positive Threshold still means what it meant (no change for saved circuits).
"""

from harness import run_test


def build(sim, vs):
    c = sim.cli
    c("circuit", "new", "--discard")
    r = c("comp", "add", "Mosfet", "--label", "M1", "--at", "0,0", "Threshold=-2V")
    c("comp", "add", "Battery", "--label", "VD", "--at", "-160,-80", "Voltage=12")
    c("comp", "add", "Resistor", "--label", "RD", "--at", "-80,-80", "Resistance=1k")
    c("comp", "add", "Battery", "--label", "VS", "--at", "-160,80", "Voltage=%g" % vs)
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,160")
    c("wire", "add", "VD.lPin", "RD.lPin")
    c("wire", "add", "RD.rPin", "M1.Dren")
    c("wire", "add", "VD.rPin", "G.Gnd")
    c("wire", "add", "M1.Sour", "VS.lPin")
    c("wire", "add", "VS.rPin", "G.Gnd")
    c("wire", "add", "M1.Gate", "G.Gnd")
    return r


def test(sim):
    r = build(sim, 1.0)
    sim.check("comp add Threshold=-2V: no 'stored' note", int("stored" in r.get("note", "")), 0, absolute=0)
    props = {p["name"]: p["value"] for p in sim.cli("comp", "get", "M1")["properties"]}
    sim.log("stored Threshold: %s" % props.get("Threshold"))
    sim.check("Threshold stored as -2 V", int(props.get("Threshold", "").startswith("-2")), 1, absolute=0)
    sim.run("5ms")
    on = abs(sim.read("RD")["current"])
    sim.check("V_GS = -1 V (above pinch-off): conducts > 100 uA (%.3g A)" % on, int(on > 100e-6), 1, absolute=0)
    build(sim, 3.0)
    sim.run("5ms")
    off = abs(sim.read("RD")["current"])
    sim.check("V_GS = -3 V (below pinch-off): off < 1 uA (%.3g A)" % off, int(off < 1e-6), 1, absolute=0)

    sim.cli("comp", "set", "M1", "Threshold=2V")
    sim.run("5ms")
    off = abs(sim.read("RD")["current"])
    sim.check("positive Threshold 2 V, V_GS = -3 V: off as before (%.3g A)" % off, int(off < 1e-6), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
