"""BH-009: WaveGen with a positive Phase shift outputs voltages below its low level.

Saw, 1 Hz, Mid_Volt 2.5 V, Semi_Ampli 2.5 V: the output must stay within 0..5 V.
With Phase 90 the signal is the saw delayed by a quarter period; being periodic,
at t = 125 ms it is at phase (0.125 - 0.25) mod 1 = 0.875, i.e. about 4.4 V.
The bug: fmod() of a negative time stays negative, so the first quarter period gives < 0 V.
"""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test

def test(sim):
    for wt in ("Saw", "Triangle"):
        sim.cli("circuit", "new", "--discard")
        sim.cli("comp", "add", "WaveGen", "--label", "W1", "--at", "0,0", "Always_On=true", "Freq=1")
        sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
        sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
        sim.cli("wire", "add", "W1.outnod", "R1.lPin")
        sim.cli("wire", "add", "R1.rPin", "G.Gnd")
        sim.cli("comp", "set", "W1", "Wave_Type=" + wt, "Phase=90", "Semi_Ampli=2.5", "Mid_Volt=2.5")
        sim.run("125ms")
        v = sim.read("R1")["pins"]["lPin"]
        # Saw: 4.375 V; Triangle (duty 50 %): phase 0.875 -> 1-(0.875-0.5)/0.5 = 0.25 -> 1.25 V
        exp = 4.375 if wt == "Saw" else 1.25
        sim.check(wt + " Phase=90 at 125 ms (V)", v, exp, tol=0.3)
run_test(test)
