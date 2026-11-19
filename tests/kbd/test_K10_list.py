"""K10 (specs/keyboard-input.md): the Shortcuts tab in Settings.

Expected values follow the spec: the tab lists every row of the shortcut registry once,
grouped by category, plus the part letters in effect (defaults and user changes), keys
as the platform writes them (⌘ ⇧ ⌥ ⌃ on macOS, Ctrl+ Shift+ Alt+ Meta+ elsewhere). The
tab and `kbd shortcuts --table` are built by the same function (Shortcuts::table); the
dialog itself is checked in the GUI (a headless server closes it). Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402

MAC = sys.platform == "darwin"


def rows_of(table, cat):
    return [r for r in table if r["category"] == cat]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    reg = sim.cli("kbd", "shortcuts")
    tab = sim.cli("kbd", "shortcuts", "--table")

    # Every registry row once, same action and context
    other = [r for r in tab if r["category"] != "Part letters"]
    sim.check("every registry row, once", len(other), len(reg), absolute=0)
    sim.check("same actions", int(sorted(r["action"] for r in other) == sorted(r["action"] for r in reg)), 1,
              absolute=0)

    # Grouped: each category in one block, in the spec's order
    cats = []
    for r in tab:
        if not cats or cats[-1] != r["category"]:
            cats.append(r["category"])
    sim.check("each category in one block", int(len(cats) == len(set(cats))), 1, absolute=0, note=str(cats))
    sim.check("File first, Switches last", int(cats[0] == "File" and cats[-1] == "Switches"), 1, absolute=0,
              note=str(cats))

    # Keys in this platform's notation
    keys = {r["action"]: r["keys"] for r in tab}
    want = {
        "Redo": "⇧⌘Z or ⌘Y" if MAC else "Ctrl+Shift+Z or Ctrl+Y",
        "Zoom to fit the whole circuit": "⌥⌘0" if MAC else "Ctrl+Alt+0",
        "Select the previous part": "⇧ Tab" if MAC else "Shift+Tab",
        "Settings": "⌘," if MAC else "Ctrl+,",
        "Start / stop the simulation": "F5",
    }
    for action, k in want.items():
        sim.check("keys: " + action, int(keys.get(action) == k), 1, absolute=0, note=str(keys.get(action)))
    sel = keys.get("Zoom to fit the selection", "")
    sim.check("keys: Zoom to fit the selection",       # Qt orders Meta and Ctrl (non-macOS: untested)
              int(sel == "⌃⌘0" if MAC else ("Meta" in sel and "Ctrl" in sel and sel.endswith("0"))), 1,
              absolute=0, note=sel)
    if MAC:
        allkeys = " ".join(r["keys"] for r in tab)
        sim.check("macOS: no Cmd / Opt words", int("Cmd" not in allkeys and "Opt" not in allkeys), 1, absolute=0)

    # Part letters in effect: the defaults, then a change
    letters = {r["keys"]: r["action"] for r in rows_of(tab, "Part letters")}
    sim.check("part letters: r adds a resistor", int(letters.get("r") == "Add Resistor"), 1, absolute=0,
              note=str(letters.get("r")))
    sim.check("part letters: sorted a–z", int(list(letters) == sorted(letters)), 1, absolute=0)
    sim.check("part letters: h w k never", int(not any(k in letters for k in "hwk")), 1, absolute=0)
    sim.cli("kbd", "partkey", "Capacitor", "r", "--replace")
    tab = sim.cli("kbd", "shortcuts", "--table")
    letters = [r for r in rows_of(tab, "Part letters")]
    r_rows = [r["action"] for r in letters if r["keys"] == "r"]
    sim.check("after a change: r adds a capacitor", int(r_rows == ["Add Capacitor"]), 1, absolute=0, note=str(r_rows))
    sim.check("after a change: the resistor has no row", int(not any(r["action"] == "Add Resistor" for r in letters)),
              1, absolute=0)
    sim.check("after a change: c is gone", int(not any(r["keys"] == "c" for r in letters)), 1, absolute=0)

    # The tab opens (headless: the dialog is closed as it shows; no error)
    sim.cli("view", "settings", "shortcuts")
    sim.check("view settings shortcuts: accepted", 1, 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
