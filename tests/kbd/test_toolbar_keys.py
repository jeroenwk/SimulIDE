"""Toolbar keys (specs/keyboard-input.md, toolbar shortcuts): every toolbar button with a
key shows it in its tooltip, as the platform writes it (⌘, on macOS, Ctrl+, elsewhere);
the keys do what the button does.

Expected values follow the spec's table: Cmd+, settings, Cmd+P export, Opt+Cmd+0 zoom to
fit, Ctrl+Cmd+0 zoom to the selection, F5 start / stop, F6 pause / resume, F7 current
flow, F8 capacitor / inductor display, F9 part states, F10 instruments, F12 noise; and
the existing file, undo / redo and 100 % keys. Keys are typed through Qt's shortcut map,
as a real key. Settings and export open dialogs, which a headless server closes: only
their binding is checked here.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402

MAC = sys.platform == "darwin"

# Button text (start of it) -> the key as the platform writes it
KEYS = {
    "Settings":                  "⌘," if MAC else "Ctrl+,",
    "Export as Image":           "⌘P" if MAC else "Ctrl+P",
    "New Circuit":               "⌘N" if MAC else "Ctrl+N",
    "Open Circuit":              "⌘O" if MAC else "Ctrl+O",
    "Save Circuit As":           "⇧⌘S" if MAC else "Ctrl+Shift+S",
    "Undo":                      "⌘Z" if MAC else "Ctrl+Z",
    "Redo":                      "⇧⌘Z" if MAC else "Ctrl+Shift+Z",
    "Zoom to fit":               "⌥⌘0" if MAC else "Ctrl+Alt+0",
    "Zoom to selected":          "⌃⌘0" if MAC else "Ctrl+Meta+0",
    "Zoom to Scale 1":           "⌘0" if MAC else "Ctrl+0",
    "Start Simulation":          "F5",
    "Pause Simulation":          "F6",
    "Show Current Flow":         "F7",
    "Show Capacitor Charge":     "F8",
    "Show Part States":          "F9",
    "Show Instruments":          "F10",
    "Transient Noise":           "F12",
}


def bar(sim):
    return sim.cli("ui", "toolbar")


def button(sim, start):
    for b in bar(sim):
        if b["text"].startswith(start):
            return b
    return None


def zoom(sim):
    return sim.cli("view", "zoom", "1")["zoom"]


def test(sim):
    sim.cli("circuit", "new", "--discard")

    # Tooltips: the text and the key, in this platform's notation
    for start, key in KEYS.items():
        b = button(sim, start)
        tip = b["tooltip"] if b else ""
        sim.check("tooltip " + start, int(tip.endswith("(" + key + ")")), 1, absolute=0, note=tip)
    tips = " ".join(b["tooltip"] for b in bar(sim))
    if MAC:
        sim.check("macOS: no Ctrl in the tooltips", int("Ctrl" not in tips), 1, absolute=0, note=tips)
    sim.check("tooltips: no tab, no &", int("\t" not in tips and "&" not in tips), 1, absolute=0)

    # Settings and export: bound (their dialogs are checked in the GUI)
    for start in ("Settings", "Export as Image"):
        b = button(sim, start)
        sim.check(start + ": key bound", int(b is not None and b["shortcut"] == KEYS[start]), 1, absolute=0,
                  note=str(b and b["shortcut"]))

    # The view toggles: each key flips its button, twice back
    for start, k in (("Show Current Flow", "F7"), ("Show Capacitor Charge", "F8"), ("Show Part States", "F9"),
                     ("Show Instruments", "F10"), ("Transient Noise", "F12")):
        before = button(sim, start)["checked"]
        sim.cli("ui", "type", k)
        after = button(sim, start)["checked"]
        sim.cli("ui", "type", k)
        back = button(sim, start)["checked"]
        sim.check(k + " toggles " + start, int(after != before and back == before), 1, absolute=0,
                  note="%s %s %s" % (before, after, back))

    # F5 starts and stops, F6 pauses and resumes (a resistor and a battery: something to run)
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "300,100")
    sim.cli("ui", "type", "F5")
    sim.check("F5: running", int(button(sim, "Stop Simulation") is not None), 1, absolute=0)
    sim.cli("ui", "type", "F6")
    sim.check("F6: paused", int(button(sim, "Resume Simulation") is not None), 1, absolute=0)
    sim.cli("ui", "type", "F6")
    sim.check("F6 again: resumed", int(button(sim, "Pause Simulation") is not None), 1, absolute=0)
    sim.check("the tooltip follows the text", int(button(sim, "Stop Simulation")["tooltip"] == "Stop Simulation (F5)"),
              1, absolute=0, note=str(button(sim, "Stop Simulation")))
    sim.cli("ui", "type", "F5")
    sim.check("F5 again: stopped", int(button(sim, "Start Simulation") is not None), 1, absolute=0)

    # Zoom: Opt+Cmd+0 fits all, Ctrl+Cmd+0 fits the selection, Cmd+0 100 %
    sim.cli("ui", "type", "cmd+0")
    sim.check("cmd+0: 100 %", zoom(sim), 1, absolute=1e-6)
    sim.cli("ui", "type", "cmd+alt+0")
    z_fit = zoom(sim)
    sim.check("cmd+alt+0: zoomed to fit", int(abs(z_fit - 1) > 0.05), 1, absolute=0, note=str(z_fit))
    sim.cli("select", "R1")
    sim.cli("ui", "type", "cmd+meta+0")
    z_sel = zoom(sim)
    sim.check("cmd+meta+0: zoomed to the selection (closer)", int(z_sel > z_fit * 1.2), 1, absolute=0,
              note="%s %s" % (z_fit, z_sel))
    sim.cli("ui", "type", "Esc", "cmd+0")
    sim.check("cmd+0 again: 100 %", zoom(sim), 1, absolute=1e-6)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    for k in ("Cmd+,", "Cmd+P", "Opt+Cmd+0", "Ctrl+Cmd+0", "F5", "F6", "F7", "F8", "F9", "F10", "F12"):
        sim.check("registry: " + k, int(any(r["keys"] == k for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
