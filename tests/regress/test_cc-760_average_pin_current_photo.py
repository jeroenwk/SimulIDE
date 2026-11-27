"""sim average gives a pin's current, and a photo part's current includes its photocurrent
(create-circuit #760, #735).

- A pin channel (VIN.outnod) gave its voltage only, so the input current of a circuit fed by a
  Fixed Voltage could not be averaged. Expected: 5 V into 1 k: 5 mA into R at R.lPin, -5 mA
  into the source at its output (it delivers 5 mA).
- `sim average D1` of a Photodiode gave ~1e-19 A: its photocurrent flows through internal
  pins. Expected: 100 lx x 70 nA/lx = 7 uA, shorted by 1 k: -7 uA into the anode (the current
  leaves the anode into the resistor), as `read` gives it.
- A Phototransistor's collector pin includes the photocurrent, as `read`'s i_collector.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-80,0", "Voltage=5", "Out=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,0", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "80,40")
    c("wire", "add", "V.outnod", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")
    r = c("sim", "average", "--for", "1ms", "R.lPin", "V.outnod")
    ch = {x["ref"]: x for x in r["channels"]}
    for ref, want in (("R.lPin", 5e-3), ("V.outnod", -5e-3)):
        cur = ch[ref].get("current")
        if cur is None:
            sim.check("%s: no current in the average" % ref, 0, 1, absolute=0)
        else:
            sim.check("%s mean current" % ref, cur["mean"], want, tol=0.01)
    c("sim", "stop")

    c("circuit", "new", "--discard")
    c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,80", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,160")
    c("wire", "add", "D.anode", "R.lPin")
    c("wire", "add", "D.cathode", "R.rPin")
    c("wire", "add", "D.anode", "G.Gnd")
    r = c("sim", "average", "--for", "1ms", "D", "D.anode")
    ch = {x["ref"]: x for x in r["channels"]}
    sim.check("Photodiode D: mean current (A)", ch["D"]["current"]["mean"], -7e-6, tol=0.02)
    sim.check("Photodiode D.anode: mean current (A)", (ch["D.anode"].get("current") or {"mean": 0})["mean"], -7e-6, tol=0.02)
    c("sim", "stop")

    c("circuit", "new", "--discard")
    c("comp", "add", "Phototransistor", "--label", "Q", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,-80", "Resistance=1000")
    c("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "160,-160", "Voltage=10", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,160")
    c("wire", "add", "VC.outnod", "R.rPin")
    c("wire", "add", "R.lPin", "Q.collector")
    c("wire", "add", "Q.emitter", "G.Gnd")
    r = c("sim", "average", "--for", "2ms", "Q.collector", "R")
    ch = {x["ref"]: x for x in r["channels"]}
    ir = -ch["R"]["current"]["mean"]          # into R at lPin is negative: it flows R.rPin -> lPin
    sim.check("Phototransistor collector current = resistor current", (ch["Q.collector"].get("current") or {"mean": 0})["mean"], ir, tol=0.01)


run_test(test)
