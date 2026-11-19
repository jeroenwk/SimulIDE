"""Keyboard layouts (specs/keyboard-input.md D21): the same commands on US QWERTY,
German QWERTZ and French AZERTY, typed as macOS sends them (ui type --layout).

Expected values follow the spec: commands match the character typed, so they work on
every layout; digits are digits on every layout, and the number row also counts as
digits without Shift (AZERTY & é " ...), but not with Shift where it types symbols
that are commands (QWERTZ: = is Shift+0, / is Shift+7) — those keep their command
even with a part selected that takes a typed value. Nothing is simulated.
"""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def prop(sim, comp, name):
    props = sim.cli("comp", "get", comp, "--all")["properties"]
    if isinstance(props, dict):
        v = props.get(name)
        return v.get("value") if isinstance(v, dict) else v
    for p in props:
        if p.get("name") == name:
            return p.get("value")
    return None


def typ(sim, layout, *keys):
    return sim.cli("ui", "type", *keys, "--layout", layout)


def two_wired(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "300,100")
    sim.cli("wire", "add", "R1.rPin", "R2.lPin")


def test(sim):
    for lay in ("us", "de", "fr"):
        # Typing a value: digits (with Shift on AZERTY), a multiplier, Enter
        sim.cli("circuit", "new", "--discard")
        sim.cli("ui", "click", "100,100")
        s = typ(sim, lay, "r")
        r = s["selection"][0]
        typ(sim, lay, "4", "7", "k", "enter")
        sim.check(lay + ": 47 kΩ typed", int(prop(sim, r, "Resistance") == "47 kΩ"), 1, absolute=0,
                  note=prop(sim, r, "Resistance"))
        # A negative value with -
        s = typ(sim, lay, "x")
        x = s["selection"][0]
        typ(sim, lay, "-", "5", "enter")
        sim.check(lay + ": -5 V typed", int(prop(sim, x, "Voltage") == "-5 V"), 1, absolute=0,
                  note=prop(sim, x, "Voltage"))

        # = with a resistor selected: network mode, not a value edit (QWERTZ: Shift+0)
        two_wired(sim)
        sim.cli("select", "R1")
        s = typ(sim, lay, "=")
        sim.check(lay + ": = network mode", int("network" in s and "valueEdit" not in s), 1, absolute=0,
                  note=str({k: s.get(k) for k in ("network", "valueEdit")}))
        typ(sim, lay, "=")

        # < and > step the pin; Cmd+> also sets pinFrom (US: Shift+, and Shift+.)
        sim.cli("select", "R1")
        p0 = sim.cli("kbd", "state")["selectedPin"]
        s = typ(sim, lay, "<")
        sim.check(lay + ": < steps the pin", int(s["selectedPin"] != p0 and "valueEdit" not in s), 1, absolute=0,
                  note=str(s["selectedPin"]))
        s = typ(sim, lay, ">")
        sim.check(lay + ": > steps it back", int(s["selectedPin"] == p0), 1, absolute=0, note=str(s["selectedPin"]))
        s = typ(sim, lay, "cmd+>")
        sim.check(lay + ": cmd+> sets pinFrom", int(s["pinFrom"] is not None and s["pinFrom"] == s["selectedPin"]), 1,
                  absolute=0, note="%s %s" % (s["pinFrom"], s["selectedPin"]))

        # : and / step the component list, with a resistor selected (QWERTZ /: Shift+7)
        sim.cli("select", "R1")
        a = typ(sim, lay, ":").get("libraryPart")
        s = typ(sim, lay, ":")
        s = typ(sim, lay, "/")
        sim.check(lay + ": / steps the list back", int(s.get("libraryPart") == a and "valueEdit" not in s), 1,
                  absolute=0, note=str({k: s.get(k) for k in ("libraryPart", "valueEdit")}))

        # + draws from the wire under the cross; Esc cancels
        sim.cli("ui", "click", "196,108")
        s = typ(sim, lay, "+")
        sim.check(lay + ": + draws a wire", int("drawing" in s), 1, absolute=0)
        typ(sim, lay, "esc")

    # A shifted number-row symbol is not a digit on QWERTY (Shift+1 = !), but the
    # unshifted AZERTY row is (& = 1)
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    s = typ(sim, "us", "r")
    s = typ(sim, "us", "!")
    sim.check("us: ! is not a digit", int("valueEdit" not in s), 1, absolute=0)
    s = typ(sim, "fr", "&", "é")
    sim.check("fr: & é are 1 2", int(s.get("valueEdit", {}).get("buffer") == "12"), 1, absolute=0,
              note=str(s.get("valueEdit")))


if __name__ == "__main__":
    run_test(test)
