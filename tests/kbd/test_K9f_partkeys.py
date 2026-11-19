"""K9f (specs/keyboard-input.md): a part's keyboard shortcut, set in its properties.

Expected values follow the spec: each kind of part in the component list has at most
one lowercase letter (a–z, h w k reserved); a letter another part has is only taken
with a confirmation (--replace here, the Replace button in the dialog), and that part
is then left without one; the letter places the part on the canvas. The CLI drives the
same code as the dialog (kbd partkey); on a headless server nothing is saved.
Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def err(sim, *args):
    """The error text of a CLI call that must fail ("" if it did not)."""
    try:
        sim.cli(*args)
    except Exception as e:  # noqa: BLE001 - the harness raises on CLI errors
        return str(e)
    return ""


def test(sim):
    sim.cli("circuit", "new", "--discard")
    r = sim.cli("kbd", "partkey", "Resistor")
    sim.check("Resistor: r by default", int(r["key"] == "r" and r["default"] == "r"), 1, absolute=0, note=str(r))

    # A letter another part has: refused, naming that part, unless replaced
    e = err(sim, "kbd", "partkey", "Capacitor", "r")
    sim.check("taken letter: refused", int(e != ""), 1, absolute=0)
    sim.check("taken letter: names the part that has it", int("Resistor" in e), 1, absolute=0, note=e)
    sim.check("refused: Capacitor keeps c", int(sim.cli("kbd", "partkey", "Capacitor")["key"] == "c"), 1, absolute=0)

    c = sim.cli("kbd", "partkey", "Capacitor", "r", "--replace")
    sim.check("replace: Capacitor has r", int(c["key"] == "r"), 1, absolute=0, note=str(c))
    sim.check("replace: Resistor has none", int(sim.cli("kbd", "partkey", "Resistor")["key"] == ""), 1, absolute=0)
    sim.check("replace: c is free", int(sim.cli("kbd", "partkey", "Capacitor")["key"] != "c"), 1, absolute=0)

    # The letter places the part
    sim.cli("ui", "click", "100,100")
    s = sim.cli("ui", "type", "r")
    sim.check("r now places a capacitor", int(bool(s["selection"]) and s["selection"][0].startswith("Capacitor")), 1,
              absolute=0, note=str(s["selection"]))

    # Reserved and non-letters: refused with the reason
    for k, why in (("h", "pan"), ("w", "wire"), ("k", "component list"), ("1", "lowercase letter")):
        e = err(sim, "kbd", "partkey", "Resistor", k)
        sim.check("refused " + k, int(why in e), 1, absolute=0, note=e)
    # A capital becomes its lowercase letter
    r = sim.cli("kbd", "partkey", "Resistor", "Q", "--replace")
    sim.check("capital Q: q", int(r["key"] == "q"), 1, absolute=0, note=str(r))
    sim.check("q taken from the UJT", int(sim.cli("kbd", "partkey", "UJT")["key"] == ""), 1, absolute=0)

    # By a part on the canvas; none clears
    sim.cli("comp", "add", "Diode", "--label", "D1", "--at", "300,100")
    d = sim.cli("kbd", "partkey", "D1", "none")
    sim.check("by a placed part: its kind (Diode)", int(d["name"] == "Diode" and d["key"] == ""), 1, absolute=0,
              note=str(d))
    sim.check("cleared: d places nothing", int("Diode" not in str(sim.cli("ui", "type", "Esc", "d")["selection"])), 1,
              absolute=0)
    sim.cli("kbd", "partkey", "Diode", "d")

    # (The dialog's Shortcut row and its Replace / Cancel box: checked in the GUI, a
    # headless server closes every dialog as it opens)

    # Registry / unknown part
    e = err(sim, "kbd", "partkey", "NoSuchPart")
    sim.check("unknown part: error", int("no such part" in e), 1, absolute=0, note=e)


if __name__ == "__main__":
    run_test(test)
