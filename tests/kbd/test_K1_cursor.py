"""K1 + K1 rev (specs/keyboard-input.md): the keyboard cursor, independent of the mouse.

Expected values follow the spec as revised (D11, D14, D15): the cross starts centred in the
view and does NOT follow the mouse; a plain left click on empty canvas places it (snap to the
drawn grid, lines at 4 + 8k); arrows move it 64 px, Shift+arrows 8 px while nothing is
selected; a key hides the mouse pointer, a mouse move shows it again; the cross shows only
while the canvas has focus and no part is selected, and follows a moved selection;
h starts pan mode (arrows pan, the cross stays, any other key ends it); Cmd+= zooms in,
Cmd+- out, Cmd+0 back to 100 % keeping the cross in place. Nothing is simulated.
"""

import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def xy(s):
    return s["cursor"]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    s = sim.cli("ui", "focus", "canvas")
    sim.check("focus: cross shown", int(s["cursorVisible"]), 1, absolute=0)
    c0 = xy(s)

    # The cross does not follow the mouse; a mouse move shows the pointer again
    s = sim.cli("ui", "mouse", "10,10")
    sim.check("mouse move: cross stays: x", xy(s)[0], c0[0], absolute=0)
    sim.check("mouse move: cross stays: y", xy(s)[1], c0[1], absolute=0)

    # A click on empty canvas places the cross (grid: 4 + 8k)
    s = sim.cli("ui", "click", "100,50")
    sim.check("click places the cross: x", xy(s)[0], 100, absolute=0)
    sim.check("click places the cross: y", xy(s)[1], 52, absolute=0)

    # Arrows move it (nothing selected): Cmd 64 px, plain 8 px (D23); a key hides the pointer
    s = sim.cli("ui", "type", "cmd+Right")
    sim.check("Cmd+Right: +64", xy(s)[0], 164, absolute=0)
    sim.check("key: pointer hidden", int(s["pointerHidden"]), 1, absolute=0)
    s = sim.cli("ui", "type", "Right", "Right", "Right")
    sim.check("Right x3: +24", xy(s)[0], 188, absolute=0)
    s = sim.cli("ui", "mouse", "0,0")
    sim.check("mouse move: pointer shown", int(s["pointerHidden"]), 0, absolute=0)
    sim.check("mouse move: cross still there", xy(s)[0], 188, absolute=0)

    # Pan mode: h on, arrows pan the canvas (the cross stays), any other key ends it
    s = sim.cli("ui", "type", "h")
    sim.check("h: pan mode on", int(s["panMode"]), 1, absolute=0)
    s = sim.cli("ui", "type", "Left", "Up")
    sim.check("pan: the cross stays", xy(s)[0], 188, absolute=0)
    sim.check("pan: still on", int(s["panMode"]), 1, absolute=0)
    s = sim.cli("ui", "type", "esc")
    sim.check("any other key ends pan mode", int(s["panMode"]), 0, absolute=0)

    # Zoom keys: in, out, back to 100 %
    # ("cmd+=" cannot be typed through the CLI: its parser reads a=b as a setting;
    #  "cmd++" is the same zoom-in path (Key_Plus instead of Key_Equal))
    z0 = sim.cli("kbd", "state")["zoom"]
    s = sim.cli("ui", "type", "cmd++")
    sim.check("Cmd++ zooms in", s["zoom"], z0 * 1.25, tol=0.01)
    s = sim.cli("ui", "type", "cmd+-")
    sim.check("Cmd+- zooms out", s["zoom"], z0, tol=0.01)
    sim.check("zoom keeps the cross", xy(s)[0], 188, absolute=0)
    s = sim.cli("ui", "type", "cmd+0")
    sim.check("Cmd+0: 100 %", s["zoom"], 1.0, tol=0.01)

    # The cross shows only while no part is selected; a click on a part does not move it
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "200,200")
    s = sim.cli("ui", "click", "200,200")
    sim.check("click on a part selects it", len(s["selection"]), 1, absolute=0)
    sim.check("part selected: cross hidden", int(s["cursorVisible"]), 0, absolute=0)
    centre = sim.cli("comp", "get", "R1")["pos"]
    snap = lambda v: 8*math.floor((v-4)/8+0.5)+4          # The cross grid: 4 + 8k
    sim.check("click on a part: cross at its centre: x", xy(s)[0], snap(centre[0]), absolute=0)
    sim.check("click on a part: cross at its centre: y", xy(s)[1], snap(centre[1]), absolute=0)
    s = sim.cli("ui", "click", "300,300")
    sim.check("click on empty: selection cleared", len(s["selection"]), 0, absolute=0)
    sim.check("no part selected: cross shown again", int(s["cursorVisible"]), 1, absolute=0)
    sim.check("cross moved to the click", xy(s)[0], 300, absolute=0)

    # Focus: cross gone when the canvas loses it, back where it was when it returns
    s = sim.cli("ui", "focus", "none")
    sim.check("no focus: cross hidden", int(s["cursorVisible"]), 0, absolute=0)
    s = sim.cli("ui", "focus", "canvas")
    sim.check("focus back: cross shown", int(s["cursorVisible"]), 1, absolute=0)
    sim.check("focus back: same place", xy(s)[0], 300, absolute=0)

    # The (hidden) cross follows a moved selection: Esc shows it where the part ended
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "cmd+Right")
    sim.check("moved part: R1 at 264", sim.cli("comp", "get", "R1")["pos"][0], 264, absolute=0)
    sim.check("moved part: cross follows", xy(s)[0], snap(centre[0])+64, absolute=0)
    sim.check("part selected: cross hidden while moving", int(s["cursorVisible"]), 0, absolute=0)

    # Existing shortcuts still reach the circuit
    sim.cli("select", "--none")
    s = sim.cli("ui", "type", "cmd+a")
    sim.check("Cmd+A selects all", len(s["selection"]), 1, absolute=0)

    # Highlight colour: a theme role, per preset
    sim.cli("settings", "set", "colorTheme", "Dark")
    sim.check("Dark highlight is yellow", int(sim.cli("settings", "get", "highlightColor") == "#ffd200"), 1, absolute=0)
    sim.cli("settings", "set", "colorTheme", "Classic")
    sim.check("Classic highlight", int(sim.cli("settings", "get", "highlightColor") == "#e05a00"), 1, absolute=0)

    # The registry lists the revised shortcuts
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: arrows move the cross", int(any(r["keys"] == "Arrows" and r["context"] == "Nothing selected" for r in sc)), 1, absolute=0)
    sim.check("registry: pan mode", int(any(r["keys"] == "h" and "pan" in r["action"].lower() for r in sc)), 1, absolute=0)
    sim.check("registry: zoom in", int(any(r["keys"].startswith("Cmd+=") for r in sc)), 1, absolute=0)
    sim.check("registry: click places the cross", int(any("Click on empty canvas" in r["keys"] for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
