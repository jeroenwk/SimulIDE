"""Where a part is: a highlight square flashes round a part selected by the keyboard (Tab,
Shift+Tab, Space) or from the Parts pane, not by the mouse (specs/parts-pane.md §6).

kbd state's "flashing" lists the parts whose highlight is still drawn (0.45 s). App setting
flashSelect (Settings dialog, Circuit/flashSelect in the ini) turns it off.
Nothing is simulated.
"""

import os
import sys
import time

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def ok(sim, name, cond, note=""):
    sim.check(name, int(bool(cond)), 1, absolute=0, note=str(note)[:300])


def test(sim):
    sim.cli("settings", "set", "flashSelect", "true")   # The user's settings may have it off
    sim.cli("circuit", "new", "--discard")
    r1 = sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")["uid"]
    r2 = sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "300,100")["uid"]

    s = sim.cli("ui", "click", "100,100")
    ok(sim, "mouse click: selected, no flash", s["selection"] == [r1] and s["flashing"] == [], s)
    s = sim.cli("ui", "type", "tab")
    ok(sim, "Tab: the next part flashes", s["selection"] == [r2] and s["flashing"] == [r2], s)
    s = sim.cli("ui", "type", "shift+tab")
    ok(sim, "Shift+Tab flashes", s["flashing"] == [r1], s)
    time.sleep(0.6)
    s = sim.cli("kbd", "state")
    ok(sim, "gone after 0.45 s", s["flashing"] == [], s["flashing"])
    sim.cli("ui", "type", "esc")
    sim.cli("ui", "click", "290,130")
    s = sim.cli("ui", "type", "space")
    ok(sim, "Space (nearest part) flashes", s["flashing"] == [r2], s)
    sim.cli("ui", "type", "esc")
    sim.cli("ui", "drag", "40,40", "360,160")
    s = sim.cli("kbd", "state")
    ok(sim, "rubber band: no flash", s["flashing"] == [], s)
    sim.cli("select", "--none")
    sim.cli("view", "parts", "--show", "R2")
    s = sim.cli("kbd", "state")
    ok(sim, "Parts pane row: flashes", s["flashing"] == [r2], s)

    sim.cli("settings", "set", "flashSelect", "false")
    ok(sim, "setting off: no flash left", sim.cli("kbd", "state")["flashing"] == [])
    sim.cli("ui", "click", "100,100")
    s = sim.cli("ui", "type", "tab")
    ok(sim, "setting off: Tab does not flash", s["selection"] == [r2] and s["flashing"] == [], s)
    ok(sim, "setting reads back", sim.cli("settings", "get", "flashSelect") is False)
    sim.cli("settings", "set", "flashSelect", "true")


if __name__ == "__main__":
    run_test(test)
