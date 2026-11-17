"""WaveGen output impedance (create-circuit #386) and the Current Source's label (#390).

#386: the WaveGen was an ideal voltage source (1 nOhm). Wired straight to a transistor's
base divider it clamped the node to its waveform and the bias was lost; a real function
generator has 50 Ohm (audio generators 600 Ohm). New property Out_Imped (Ohm, default 0 =
ideal as before). Right result, a constant output (Square, Duty 100 %: high = Mid_Volt +
Semi_Ampli = 2 V) into a 50 Ohm load:
- default: 2 V on the load (unchanged);
- Out_Imped=50: divider 50/(50+50): 1 V;
- bipolar (Mid_Volt 0, Semi_Ampli 1: Out-Gnd = +1 V at the top of the square), Out_Imped=100,
  100 Ohm across Out-Gnd: 0.5 V across the load, floating or not (not floating, each pin
  is a ground-referenced source of half the impedance, in series).

#390: the Current Source's value label showed MaxValue (1 A, the dial's full scale), not
the current it drives. Right result: a new part shows Value_Amp.
"""

import os, re, sys, tempfile
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def load_volts(sim, props=(), bipolar=False, gnd_grounded=False):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,0", "Wave_Type=Square", "Duty=100",
      "Freq=1", "Mid_Volt=1", "Semi_Ampli=1", "Always_On=true", *props)
    c("comp", "add", "Resistor", "--label", "RL", "--at", "0,0", "--rotate", "90",
      "Resistance=100" if bipolar else "Resistance=50")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,120")
    c("wire", "add", "W.outnod", "RL.lPin")
    if bipolar:
        c("wire", "add", "RL.rPin", "W.gndnod")
        if gnd_grounded:
            c("wire", "add", "W.gndnod", "G.Gnd")
    else:
        c("wire", "add", "RL.rPin", "G.Gnd")
    sim.run("10ms")
    p = sim.read("RL")["pins"]
    return abs(p["lPin"] - p["rPin"])


def test(sim):
    sim.check("WaveGen default (ideal) into 50 Ohm (V)", load_volts(sim), 2.0, tol=0.01)
    sim.check("WaveGen Out_Imped=50 into 50 Ohm (V)", load_volts(sim, ("Out_Imped=50",)), 1.0, tol=0.01)
    bip = ("Bipolar=true", "Mid_Volt=0")
    sim.check("WaveGen bipolar floating, ideal, into 100 Ohm (V)",
              load_volts(sim, bip + ("Floating=true",), bipolar=True, gnd_grounded=True), 1.0, tol=0.01)
    sim.check("WaveGen bipolar floating Out_Imped=100 into 100 Ohm (V)",
              load_volts(sim, bip + ("Floating=true", "Out_Imped=100"), bipolar=True, gnd_grounded=True), 0.5, tol=0.01)
    sim.check("WaveGen bipolar Out_Imped=100 into 100 Ohm (V)",
              load_volts(sim, bip + ("Out_Imped=100",), bipolar=True), 0.5, tol=0.01)
    sim.finite("WaveGen", "W")

    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Current Source", "--label", "I", "--at", "0,0", "Value_Amp=2 mA")
    path = os.path.join(tempfile.mkdtemp(prefix="cc-lim3-"), "currsource.sim1")
    c("circuit", "save", path)
    m = re.search(r'ShowProp="([^"]*)"', open(path).read())
    shown = m.group(1) if m else None
    sim.log("  Current Source ShowProp: %s" % shown)
    sim.check("Current Source label shows Value_Amp (%s)" % shown, int(shown == "Value_Amp"), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
