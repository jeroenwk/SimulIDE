"""Locked parts (specs/parts-pane.md §10, test 9).

Expected values follow the spec:
- Cmd+Shift+K locks the selected parts (one undo step), Opt+Cmd+Shift+K unlocks them;
  the lock shows in the Parts pane rows (locked field);
- a click on a locked part goes through it (the part under it, or empty canvas); a rubber
  band leaves it out; Tab, Space and Cmd+A skip it;
- selected from the pane (CLI select), it can't be dragged, moved with the arrows, rotated,
  flipped or deleted, and Messages says so;
- the lock is saved (Locked="true", only when locked); copies are pasted unlocked;
- a locked switch still toggles while the simulation runs.
Nothing is measured.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def ok(sim, name, cond, note=""):
    sim.check(name, int(bool(cond)), 1, absolute=0, note=str(note)[:300])


def locked(sim):
    return sorted(r["label"] for r in sim.cli("parts", "list", "--locked"))


def sel(sim):
    return sorted(x["label"] for x in sim.cli("select"))


def prop(sim, comp, name):
    for p in sim.cli("comp", "get", comp, "--all")["properties"]:
        if p["name"] == name:
            return p["value"]
    return None


def near(c, p):                                   # The cross snaps to the grid near the click
    return abs(c[0] - p[0]) <= 8 and abs(c[1] - p[1]) <= 8


def msgs(sim, text):
    return sim.cli("messages", "--grep", text)


def test(sim):
    sim.cli("circuit", "new", "--discard")
    bg = sim.cli("comp", "add", "Rectangle", "--label", "BG", "--at", "400,100", "H_size=200", "V_size=160")["uid"]
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "200,100")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "400,100")       # On BG
    sim.cli("comp", "add", "Switch", "--label", "SW1", "--at", "100,300")

    # Lock with the keys: one undo step
    sim.cli("select", "R1", "BG")
    sim.cli("ui", "type", "cmd+shift+k")
    ok(sim, "cmd+shift+k locks", locked(sim) == ["BG", "R1"], locked(sim))
    ok(sim, "message Locked", msgs(sim, "Locked"), msgs(sim, "Locked"))
    sim.cli("undo")
    ok(sim, "undone in one step", locked(sim) == [], locked(sim))
    sim.cli("redo")
    ok(sim, "redone", locked(sim) == ["BG", "R1"], locked(sim))
    rows = {r["label"]: r for r in sim.cli("parts", "list", "--all")}
    ok(sim, "pane row shows the lock", rows["R1"]["locked"] and rows["R1"]["tab"] is None, rows["R1"])

    # No hand cursor over a locked part, its labels or a locked Text; back on unlock
    sim.cli("comp", "add", "TextComponent", "--label", "TX", "--at", "100,420")
    tx = [r for r in sim.cli("parts", "list", "--all") if r["label"] == "TX"][0]
    cur = lambda p: sim.cli("ui", "mouse", p)["cursor_shape"]
    ok(sim, "hand over unlocked R2", cur("200,100") == "openhand", cur("200,100"))
    ok(sim, "hand over unlocked Text", cur("108,424") == "openhand", cur("108,424"))
    sim.cli("lock", "TX")
    ok(sim, "no hand over locked R1", cur("100,100") == "view", cur("100,100"))
    ok(sim, "no hand over locked BG", cur("460,150") == "view", cur("460,150"))
    ok(sim, "no hand over locked Text", cur("108,424") == "view", cur("108,424"))
    sim.cli("unlock", "TX")
    ok(sim, "hand back on the unlocked Text", cur("108,424") == "openhand", cur("108,424"))
    sim.cli("comp", "remove", tx["uid"])

    # Mouse: clicks go through, rubber band leaves it out
    sim.cli("select", "--none")
    s = sim.cli("ui", "click", "104,96")
    ok(sim, "click on locked R1: nothing selected", sel(sim) == [], sel(sim))
    ok(sim, "click on locked R1: the cross goes there", near(s["cursor"], (104, 96)), s["cursor"])
    s = sim.cli("ui", "click", "440,140")
    ok(sim, "click on locked BG: the cross goes there", near(s["cursor"], (440, 140)), s["cursor"])
    sim.cli("ui", "click", "400,100")
    ok(sim, "click on R3 over locked BG: R3", sel(sim) == ["R3"], sel(sim))
    sim.cli("ui", "click", "460,150")
    ok(sim, "click on locked BG's empty area: nothing", sel(sim) == [], sel(sim))
    sim.cli("ui", "drag", "40,40", "260,160")
    ok(sim, "rubber band: R2, not locked R1", sel(sim) == ["R2"], sel(sim))

    # Keyboard: Tab, Space, Cmd+A skip it
    sim.cli("ui", "click", "100,240")
    walk = set()
    for _ in range(6):
        s = sim.cli("ui", "type", "tab")
        walk.update(s["selection"])
    labels = {r["uid"]: r["label"] for r in sim.cli("parts", "list", "--all")}
    ok(sim, "Tab skips locked", not {"R1", "BG"} & {labels[u] for u in walk}, [labels[u] for u in walk])
    sim.cli("select", "--none")
    sim.cli("ui", "click", "100,140")
    s = sim.cli("ui", "type", "space")
    ok(sim, "Space skips locked R1", "R1" not in [labels.get(u) for u in s["selection"]], s["selection"])
    sim.cli("ui", "type", "esc")
    sim.cli("select", "--none")
    sim.cli("ui", "type", "cmd+a")
    ok(sim, "cmd+a skips locked", not {"R1", "BG"} & set(sel(sim)) and "R2" in sel(sim), sel(sim))

    # Selected from the pane: kept, but not edited
    sim.cli("select", "R1")
    ok(sim, "select (pane) selects locked R1", sel(sim) == ["R1"], sel(sim))
    pos = sim.cli("comp", "get", "R1")["pos"]
    sim.cli("ui", "drag", "100,100", "180,180")
    ok(sim, "not dragged", sim.cli("comp", "get", "R1")["pos"] == pos, sim.cli("comp", "get", "R1")["pos"])
    sim.cli("select", "R1")
    sim.cli("ui", "type", "Right", "cmd+Down")
    ok(sim, "not moved by arrows", sim.cli("comp", "get", "R1")["pos"] == pos, sim.cli("comp", "get", "R1")["pos"])
    ok(sim, "message: not moved", msgs(sim, "locked: not moved"), msgs(sim, "locked"))
    sim.cli("select", "R1")
    body = sim.cli("comp", "get", "R1")["body"]
    sim.cli("ui", "type", "cmd+r", "cmd+l")
    ok(sim, "not rotated / flipped", sim.cli("comp", "get", "R1")["body"] == body, (body, sim.cli("comp", "get", "R1")["body"]))
    ok(sim, "message: not rotated", msgs(sim, "locked: not rotated"))
    sim.cli("select", "R1", "R2", "--add")
    sim.cli("ui", "type", "Backspace")
    names = [r["label"] for r in sim.cli("parts", "list", "--all")]
    ok(sim, "delete: R1 kept, R2 deleted", "R1" in names and "R2" not in names, names)
    ok(sim, "message: not deleted", msgs(sim, "locked: not deleted"))
    sim.cli("undo")

    # Unlock with the keys and the CLI
    sim.cli("select", "R1")
    sim.cli("ui", "type", "opt+cmd+shift+k")
    ok(sim, "opt+cmd+shift+k unlocks", locked(sim) == ["BG"], locked(sim))
    sim.cli("lock", "R1,SW1")
    ok(sim, "lock R1,SW1", locked(sim) == ["BG", "R1", "SW1"], locked(sim))

    # Saved, reopened; copies unlocked
    d = tempfile.mkdtemp(prefix="lock-")
    path = os.path.join(d, "lock.sim1")
    sim.cli("circuit", "save", path)
    xml = open(path, encoding="utf-8").read()
    ok(sim, "Locked saved only when locked", xml.count('Locked="true"') == 3 and "Locked=\"\"" not in xml)
    sim.cli("circuit", "open", path, "--discard")
    ok(sim, "lock survives reopen", locked(sim) == ["BG", "R1", "SW1"], locked(sim))
    new = sim.cli("comp", "copy", "R1")
    ok(sim, "copy pasted unlocked", locked(sim) == ["BG", "R1", "SW1"], (locked(sim), new))

    # A locked switch still works in the simulation
    sim.cli("sim", "start")
    st0 = prop(sim, "SW1", "Toggled")
    r = sim.cli("ui", "click", "SW1")
    st1 = prop(sim, "SW1", "Toggled")
    sim.cli("sim", "stop")
    ok(sim, "locked switch toggles while running", st0 != st1, (st0, st1, r))

    r = sim.cli("unlock", "--all")
    ok(sim, "unlock --all", r["locked"] == [], r)


if __name__ == "__main__":
    run_test(test)
