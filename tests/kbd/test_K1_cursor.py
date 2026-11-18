"""K1 (specs/keyboard-input.md): keyboard cursor cross, pointer hiding, focus, highlight colour.

Expected values follow the spec: the cross sits on the drawn grid (lines at 4 + 8k), Shift+Arrow
moves it 8 px when nothing is selected, a key hides the mouse pointer, a mouse move shows it and
puts the cross under it, no focus hides the cross. Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def xy(state):
    return state["cursor"]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    s = sim.cli("ui", "focus", "canvas")
    sim.check("focus: cross shown", int(s["cursorVisible"]), 1, absolute=0)

    s = sim.cli("ui", "mouse", "100,50")          # snapped to the grid point (100, 52)
    sim.check("mouse: cross x", xy(s)[0], 100, absolute=0)
    sim.check("mouse: cross y", xy(s)[1], 52, absolute=0)
    sim.check("mouse: pointer shown", int(s["pointerHidden"]), 0, absolute=0)

    s = sim.cli("ui", "type", "shift+Right", "shift+Right", "shift+Right")
    sim.check("Shift+Right x3: x", xy(s)[0], 124, absolute=0)
    sim.check("Shift+Right x3: y", xy(s)[1], 52, absolute=0)
    sim.check("key: pointer hidden", int(s["pointerHidden"]), 1, absolute=0)

    s = sim.cli("ui", "type", "shift+Up", "shift+Left")
    sim.check("Shift+Up: y", xy(s)[1], 44, absolute=0)
    sim.check("Shift+Left: x", xy(s)[0], 116, absolute=0)

    s = sim.cli("ui", "mouse", "204,204")
    sim.check("mouse again: cross x", xy(s)[0], 204, absolute=0)
    sim.check("mouse again: cross y", xy(s)[1], 204, absolute=0)
    sim.check("mouse again: pointer shown", int(s["pointerHidden"]), 0, absolute=0)

    s = sim.cli("ui", "focus", "none")
    sim.check("no focus: cross hidden", int(s["cursorVisible"]), 0, absolute=0)
    s = sim.cli("ui", "focus", "canvas")
    sim.check("focus back: cross shown", int(s["cursorVisible"]), 1, absolute=0)
    sim.check("focus back: same place", xy(s)[0], 204, absolute=0)

    # A selection: Shift+Arrow is not the cursor's any more (K2 moves the part)
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "300,300")
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "shift+Right")
    sim.check("selection: cursor stays", xy(s)[0], 204, absolute=0)

    # Existing shortcuts still reach the circuit
    sim.cli("select", "--none")
    s = sim.cli("ui", "type", "cmd+a")
    sim.check("Cmd+A selects all", len(s["selection"]), 1, absolute=0)

    # Highlight colour: a theme role, per preset
    sim.cli("settings", "set", "colorTheme", "Dark")
    sim.check("Dark highlight is yellow", int(sim.cli("settings", "get", "highlightColor") == "#ffd200"), 1, absolute=0)
    sim.cli("settings", "set", "colorTheme", "Classic")
    sim.check("Classic highlight", int(sim.cli("settings", "get", "highlightColor") == "#e05a00"), 1, absolute=0)

    # The registry lists the K1 shortcut
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry has Shift+Arrows", int(any(r["keys"] == "Shift+Arrows" for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
