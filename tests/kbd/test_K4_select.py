"""K4 (specs/keyboard-input.md): Tab / Shift+Tab part selection, Esc, Enter, selection rule.

Expected values follow the spec: Tab / Shift+Tab select the next / previous part in reading
order (rows top to bottom, centres within 16 px are one row, then left to right), wrapping;
with nothing selected, the part nearest the keyboard cursor. When the selection becomes one
part B, pinFrom = the pin of the last part A and pinTo = B's pin; auto pins are the nearest
pair (a pin chosen with < > is kept, the other side takes its nearest pin). Esc unselects and
keeps the last part; Enter unselects one part and moves the cross a big step beyond its
selected pin, in the pin's direction (Shift+Enter: beyond the most opposite pin). Pin positions
of a resistor are its centre +-16 px (from `comp add`). Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def sel(s):
    return s["selection"]


def uid(sim, label):
    return sim.cli("comp", "get", label)["uid"]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    for label, at in (("R1", "100,100"), ("R2", "300,108"), ("R3", "100,300"), ("R4", "300,300")):
        sim.cli("comp", "add", "Resistor", "--label", label, "--at", at)
    u = {l: uid(sim, l) for l in ("R1", "R2", "R3", "R4")}

    # Nothing selected: Tab takes the part nearest the cross (a click places it)
    sim.cli("ui", "click", "120,290")
    s = sim.cli("ui", "type", "Tab")
    sim.check("Tab, nothing selected: nearest (R3)", int(sel(s) == [u["R3"]]), 1, absolute=0, note=str(sel(s)))

    # Reading order R1 R2 | R3 R4 (R2 is 8 px lower than R1: same row), wrapping
    s = sim.cli("ui", "type", "Tab")
    sim.check("Tab: R4", int(sel(s) == [u["R4"]]), 1, absolute=0)
    s = sim.cli("ui", "type", "Tab")
    sim.check("Tab wraps: R1", int(sel(s) == [u["R1"]]), 1, absolute=0)
    s = sim.cli("ui", "type", "Tab")
    sim.check("Tab: R2 (same row as R1)", int(sel(s) == [u["R2"]]), 1, absolute=0)
    s = sim.cli("ui", "type", "shift+Tab", "shift+Tab")
    sim.check("Shift+Tab x2: R4", int(sel(s) == [u["R4"]]), 1, absolute=0)

    # Auto pins: nearest pair. From R1 (left) to R2 (right): R1.rPin -> R2.lPin
    sim.cli("circuit", "new", "--discard")
    for label, at in (("R1", "100,100"), ("R2", "300,108"), ("R3", "100,300"), ("R4", "300,300")):
        sim.cli("comp", "add", "Resistor", "--label", label, "--at", at)
    u = {l: uid(sim, l) for l in ("R1", "R2", "R3", "R4")}
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "Tab")
    sim.check("R1 -> R2: pinFrom R1.rPin", int(s["pinFrom"] == "R1.rPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("R1 -> R2: pinTo R2.lPin", int(s["pinTo"] == "R2.lPin"), 1, absolute=0, note=str(s["pinTo"]))
    sim.check("R2's selected pin follows (auto)", int(s["selectedPin"] == "R2.lPin"), 1, absolute=0)

    # Backwards: R2 -> R1 gives R2.lPin -> R1.rPin
    s = sim.cli("ui", "type", "shift+Tab")
    sim.check("R2 -> R1: pinFrom R2.lPin", int(s["pinFrom"] == "R2.lPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("R2 -> R1: pinTo R1.rPin", int(s["pinTo"] == "R1.rPin"), 1, absolute=0, note=str(s["pinTo"]))

    # A manual pin is kept; the other side takes its pin nearest to it
    sim.cli("select", "R3")
    sim.cli("ui", "type", "<")                       # R3: rPin (auto) -> lPin, manual
    s = sim.cli("ui", "type", "Tab")                 # -> R4
    sim.check("manual kept: pinFrom R3.lPin", int(s["pinFrom"] == "R3.lPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("other side nearest: pinTo R4.lPin", int(s["pinTo"] == "R4.lPin"), 1, absolute=0, note=str(s["pinTo"]))

    # Esc: unselect, last part kept
    s = sim.cli("ui", "type", "Esc")
    sim.check("Esc unselects", len(sel(s)), 0, absolute=0)
    sim.check("Esc keeps the last part", int(s["lastPart"] == u["R4"]), 1, absolute=0)
    sim.check("Esc keeps pinTo", int(s["pinTo"] == "R4.lPin"), 1, absolute=0)

    # Tab puts the (hidden) cross at the selected part's centre: Esc shows it there
    sim.cli("ui", "type", "Tab")                     # Selects a part (reading order)
    s = sim.cli("kbd", "state")
    part = s["selection"][0]
    centre = sim.cli("comp", "get", part)["pos"]
    sim.check("tab: cross hidden at the part's centre: x", s["cursor"][0], centre[0], absolute=0)
    sim.check("tab: cross hidden at the part's centre: y", s["cursor"][1], centre[1], absolute=0)
    sim.check("tab: cross hidden while selected", int(s["cursorVisible"]), 0, absolute=0)
    s = sim.cli("ui", "type", "Esc")
    sim.check("esc: cross shown at that place", int(s["cursorVisible"] and s["cursor"] == centre), 1, absolute=0,
              note=str(s["cursor"]))

    # Esc re-reads the part's position: a part that moved without the selection
    # (a library drop's ghost) leaves the cross where the part really is
    sim.cli("ui", "type", "Tab")
    part = sim.cli("kbd", "state")["selection"][0]
    sim.cli("comp", "set", part, "Pos=400,300")       # Moves it outside moveSelection
    at = sim.cli("comp", "get", part)["pos"]          # (snapped to its grid)
    s = sim.cli("ui", "type", "Esc")
    sim.check("esc: the cross where the part moved to", int(s["cursor"] == at), 1, absolute=0,
              note=str(s["cursor"]))

    # Enter: unselect, the cross a big step beyond the selected pin (D16). R1's
    # selected pin is its rPin (116,100) pointing right
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "Enter")
    sim.check("Enter unselects", len(sel(s)), 0, absolute=0)
    sim.check("Enter: cross beyond the pin: x", s["cursor"][0], 180, absolute=0)
    sim.check("Enter: cross beyond the pin: y", s["cursor"][1], 100, absolute=0)
    s = sim.cli("ui", "type", "Enter")
    sim.check("Enter with nothing selected: cross stays: x", s["cursor"][0], 180, absolute=0)

    # Shift+Enter: the same, beyond the most opposite pin (lPin at 84,100)
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "shift+Enter")
    sim.check("Shift+Enter unselects", len(sel(s)), 0, absolute=0)
    sim.check("Shift+Enter: cross beyond the opposite pin: x", s["cursor"][0], 20, absolute=0)
    sim.check("Shift+Enter: cross beyond the opposite pin: y", s["cursor"][1], 100, absolute=0)

    # A click-like change (several parts, then one other): the rule sees only the final state
    sim.cli("select", "R2")                         # last part R2 (from R1)
    sim.cli("select", "R3", "R4")                   # several: no change
    s = sim.cli("kbd", "state")
    sim.check("several selected: last part unchanged", int(s["lastPart"] == u["R2"]), 1, absolute=0)
    sim.cli("select", "R1")
    s = sim.cli("kbd", "state")
    sim.check("then one: pinFrom on R2 (not R3/R4)", int((s["pinFrom"] or "").startswith("R2.")), 1, absolute=0,
              note=str(s["pinFrom"]))

    # Moving a part re-pairs the auto pins: R1 moved to the right of R2
    sim.cli("select", "R2")                         # A = R1, B = R2, both auto
    sim.cli("select", "R1")                         # A = R2, B = R1
    s = sim.cli("ui", "type", "Right", "Right", "Right", "Right", "Right")   # R1 to x = 420
    sim.check("after the move: pinFrom R2.rPin", int(s["pinFrom"] == "R2.rPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("after the move: pinTo R1.lPin", int(s["pinTo"] == "R1.lPin"), 1, absolute=0, note=str(s["pinTo"]))

    # Deleting the last part forgets it
    sim.cli("comp", "remove", "R1")
    s = sim.cli("kbd", "state")
    sim.check("deleted last part forgotten", int(s["lastPart"] is None), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
