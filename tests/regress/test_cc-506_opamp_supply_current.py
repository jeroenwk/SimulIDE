"""An op amp's output current flows through its supply pins (create-circuit #506).

With Power_Pins the supply pins only set the rails: they carried no current, the output's
current came from ground. AoE fig 4.73 (split-supply generator) puts the ground on the
follower output and leaves the battery floating: the load current must go round through the
op amp's supply pins, and the battery island collapsed instead (VP +9.00 V, VM -1.4 mV with the
load wanting 4.5 mA; 1.35 uA reached it).

Circuit: floating 18 V battery (VP to VM), 10k/10k divider into a follower's + input, the
follower's output on ground, power pins on VP and VM. Load RL 1k:
- from VP to the output: the op amp sinks 9 mA into its V- pin; VP +9 V, VM -9 V;
- from the output to VM: it sources 9 mA from its V+ pin; same rails.
Expected: VP = +9 V, VM = -9 V (within the 1/1001 gain residue), the load current 9 mA,
the supply pin that carries it 9 mA (V+ pin into the part when sourcing, V- pin out of it
when sinking) and the other pin under 1 uA; with no load both under 1 uA.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def run(sim, load):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B", "--at", "-200,0", "Voltage=18")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "-100,-60", "Resistance=10k")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "-100,60", "Resistance=10k")
    c("comp", "add", "opAmp", "--label", "U", "--at", "0,0", "Power_Pins=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "80,100")
    c("wire", "add", "B.lPin", "U.powerPos")
    c("wire", "add", "B.rPin", "U.powerNeg")
    c("wire", "add", "B.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "R2.lPin")
    c("wire", "add", "R2.rPin", "B.rPin")
    c("wire", "add", "R1.rPin", "U.inputNinv")
    c("wire", "add", "U.output", "U.inputInv")
    c("wire", "add", "U.output", "G.Gnd")
    if load == "high":
        c("comp", "add", "Resistor", "--label", "RL", "--at", "100,-60", "Resistance=1k")
        c("wire", "add", "B.lPin", "RL.lPin")
        c("wire", "add", "RL.rPin", "U.output")
    elif load == "low":
        c("comp", "add", "Resistor", "--label", "RL", "--at", "100,60", "Resistance=1k")
        c("wire", "add", "U.output", "RL.lPin")
        c("wire", "add", "RL.rPin", "B.rPin")
    c("sim", "run", "--for", "1ms")
    vp = c("probe", "U.powerPos")[0]
    vm = c("probe", "U.powerNeg")[0]
    il = c("probe", "RL.lPin")[0]["current"] if load else 0
    c("sim", "stop")
    return vp["voltage"], vm["voltage"], vp["current"], vm["current"], il


def test(sim):
    for load in (None, "high", "low"):
        vp, vm, ip, im, il = run(sim, load)
        n = load or "no load"
        sim.check(n + ": VP (V)", vp, 9, absolute=0.02)
        sim.check(n + ": VM (V)", vm, -9, absolute=0.02)
        if load is None:
            sim.check(n + ": V+ pin current (A)", ip, 0, absolute=1e-6)
            sim.check(n + ": V- pin current (A)", im, 0, absolute=1e-6)
        else:
            sim.check(n + ": load current (A)", abs(il), 9e-3, absolute=0.1e-3)
        if load == "high":   # Sinks: the V- pin carries the load current out of the part
            sim.check(n + ": V- pin current (A)", im, -9e-3, absolute=0.1e-3)
            sim.check(n + ": V+ pin current (A)", ip, 0, absolute=1e-6)
        if load == "low":    # Sources: from the V+ pin into the part
            sim.check(n + ": V+ pin current (A)", ip, 9e-3, absolute=0.1e-3)
            sim.check(n + ": V- pin current (A)", im, 0, absolute=1e-6)


run_test(test)
