"""BH-058: the CLI marks every pin of a scope or meter "hidden" in a fresh headless instance.

clirefs.cpp sets "hidden" when !Pin::isVisible(). QGraphicsItem::isVisible() is false
too when the parent item is not visible, and a fresh headless instance reports the
Oscope / Voltmeter body so: all their pins came out hidden although they are drawn and
wire normally. Tools that skip hidden pins (create-circuit's router) then found no pin
for `SC.Pin0`. The pin's own state is isVisibleTo( parentItem() ), as cli_edit.cpp
already uses. A WaveGen's gndnod (not drawn unless Bipolar) must stay hidden.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def hidden(sim, label):
    return sorted(p["id"] for p in sim.cli("comp", "get", label)["pins"] if p.get("hidden"))


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Oscope", "--label", "SC", "--at", "400,200")
    sim.cli("comp", "add", "Voltmeter", "--label", "VM", "--at", "600,200")
    sim.cli("comp", "add", "WaveGen", "--label", "SIG", "--at", "200,200")
    sc, vm, sig = hidden(sim, "SC"), hidden(sim, "VM"), hidden(sim, "SIG")
    print("hidden: SC %s  VM %s  SIG %s" % (sc, vm, sig))
    sim.check("scope pins not hidden", len(sc), 0, absolute=0.5)
    sim.check("voltmeter pins not hidden", len(vm), 0, absolute=0.5)
    sim.check("WaveGen gndnod hidden (not Bipolar)", int(sig == ["gndnod"]), 1, absolute=0.5)


run_test(test)
