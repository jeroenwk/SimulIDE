"""A Clock whose Always_On is switched off stops (create-circuit #637).

`Always_On=true` sets the clock running (and hides its button), but `Always_On=false` only
cleared the flag: the clock kept running, so a script that turned it off saw its outputs
keep toggling. Expected: after Always_On=false (paused or stopped), the output stays low;
Always_On=true starts it again; a clock that was never always-on keeps its Running state.
"""

from harness import run_test


def toggles(c, n=8, step="0.3ms"):
    seen = set()
    for _ in range(n):
        c("sim", "run", "--for", step)
        seen.add(round(c("probe", "R.lPin")[0]["voltage"]))
    return len(seen) > 1


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Clock", "--label", "CLK", "--at", "0,0", "Freq=1kHz", "Always_On=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,0", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,40")
    c("wire", "add", "CLK.outnod", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")
    sim.check("Always_On clock toggles", int(toggles(c)), 1, absolute=0)
    c("comp", "set", "CLK", "Always_On=false")
    sim.check("Always_On=false while paused: stopped", int(toggles(c)), 0, absolute=0)
    c("comp", "set", "CLK", "Always_On=true")
    sim.check("Always_On=true again: toggles", int(toggles(c)), 1, absolute=0)
    c("sim", "stop")
    c("comp", "set", "CLK", "Always_On=false")
    sim.check("Always_On=false while stopped: stays stopped", int(toggles(c)), 0, absolute=0)
    c("sim", "stop")
    c("comp", "set", "CLK", "Running=true")
    c("comp", "set", "CLK", "Always_On=false")
    sim.check("never always-on, Running kept", int(toggles(c)), 1, absolute=0)
    c("sim", "stop")


run_test(test)
