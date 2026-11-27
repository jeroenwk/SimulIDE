"""An op amp on a rail whose supply pin moves with its output converges (create-circuit #426).

On a rail (SAT+ / SAT-) the output was a source at the supply pin's voltage of the last solve,
through Out_Imped. When that pin is not stiff, the output current through it moves it, and the
next solve put the output there: one solve behind, a fixed-point loop of gain -Rs/RL that
never settles at Rs >= RL. Seen on led_driver board_mains (tests test_floating_pair_refine):
the MT7833A's OCP comparator sits on SAT- with its ground pin on the chip's floating ground
(non-isolated buck); the node then flipped between 4.29387 and 4.29519 V on every pass and
the step took 100000 iterations, once the Newton MOSFET moved the trajectory onto such a step.

Circuits (comparator, inputs fixed, no feedback):
- SAT-: + on ground, - on +10 V. V+ on the 10 V battery, V- to ground through Rs, a load RL
  from +10 V into the output. The output sits on V-, which carries the load current:
  Vout = V- = 10*Rs/(RL+Rs).
- SAT+: + on +10 V, - on ground. V- on ground, V+ to the 10 V battery through Rs, a load RL
  from the output to ground: Vout = V+ = 10*RL/(RL+Rs).
RL = 1k; Rs = 1k (loop gain -1: a two-cycle) and 2k (gain -2: diverges).
Expected: those voltages (within 1 mV: Out_Imped 1 nOhm), no "Not converging" message.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def run(sim, rail, rs):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B", "--at", "-200,0", "Voltage=10")
    c("comp", "add", "opAmp", "--label", "U", "--at", "0,0", "Power_Pins=true")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "0,-100", "Resistance=%dk" % rs)
    c("comp", "add", "Resistor", "--label", "RL", "--at", "100,0", "Resistance=1k")
    for g in ("G1", "G2", "G3"):
        c("comp", "add", "Ground", "--label", g, "--at", "%d,150" % (-200 + 100*int(g[1])))
    c("wire", "add", "B.rPin", "G1.Gnd")
    if rail == "SAT-":
        c("wire", "add", "U.inputNinv", "G2.Gnd")
        c("wire", "add", "U.inputInv", "B.lPin")
        c("wire", "add", "U.powerPos", "B.lPin")
        c("wire", "add", "U.powerNeg", "RS.lPin")
        c("wire", "add", "RS.rPin", "G3.Gnd")
        c("wire", "add", "B.lPin", "RL.lPin")
        c("wire", "add", "RL.rPin", "U.output")
        expect = 10.0*rs/(1 + rs)
        rp = "U.powerNeg"
    else:
        c("wire", "add", "U.inputNinv", "B.lPin")
        c("wire", "add", "U.inputInv", "G2.Gnd")
        c("wire", "add", "U.powerNeg", "G2.Gnd")
        c("wire", "add", "U.powerPos", "RS.lPin")
        c("wire", "add", "RS.rPin", "B.lPin")
        c("wire", "add", "U.output", "RL.lPin")
        c("wire", "add", "RL.rPin", "G3.Gnd")
        expect = 10.0*1/(1 + rs)
        rp = "U.powerPos"
    c("sim", "run", "--for", "1ms")
    vo = c("probe", "U.output")[0]["voltage"]
    vr = c("probe", rp)[0]["voltage"]
    bad = [l for l in c("messages")["lines"] if "Not converging" in l]
    c("sim", "stop")
    return vo, vr, len(bad), expect


def test(sim):
    for rail in ("SAT-", "SAT+"):
        for rs in (1, 2):
            vo, vr, bad, expect = run(sim, rail, rs)
            n = "%s, Rs %dk" % (rail, rs)
            sim.check(n + ": output (V)", vo, expect, absolute=1e-3)
            sim.check(n + ": rail pin (V)", vr, expect, absolute=1e-3)
            sim.check(n + ": 'Not converging' messages", bad, 0, absolute=0)


run_test(test)
