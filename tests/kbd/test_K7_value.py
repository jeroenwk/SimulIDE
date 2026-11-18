"""K7 (specs/keyboard-input.md): typing the selected part's value.

Expected values follow the spec: with one part that has a numeric valueProperty()
selected, a digit (also the AZERTY number row without Shift, D10), - . or , starts
EditValue; the buffer grammar is [-]digits[.digits][mult[digits]] (4k7 = 4.7 k, u = µ);
a buffer without a multiplier keeps the part's current unit (4.7 kΩ + 100 = 100 kΩ);
capitals type multipliers too (K = k, U = u; M mega, m milli); Enter sets the property
in one undo step, unselects and moves the cross right; Esc reverts (D8): Backspace on
an empty buffer ends the edit and the next deletes the part; Tab or a click reverts
then does that action; parts without a numeric value (a scope) take no typed value.
Nothing is simulated.
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


def placed(sim):
    """The uid of the one selected part after placing it with a key."""
    return sim.cli("kbd", "state")["selection"][0]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")             # cross at (100, 100)

    # r 2 0 0 k Enter: R = 200 kΩ, unselected, cross a big step right of the part
    sim.cli("ui", "type", "r")
    uid = placed(sim)
    s = sim.cli("ui", "type", "2", "0", "0", "k")
    sim.check("typing builds the buffer", int(s["valueEdit"]["buffer"] == "200k"), 1, absolute=0)
    s = sim.cli("ui", "type", "enter")
    sim.check("enter: unselected", len(s["selection"]), 0, absolute=0)
    sim.check("enter: cross beyond the pin: x", s["cursor"][0], 180, absolute=0)
    sim.check("enter: cross beyond the pin: y", s["cursor"][1], 100, absolute=0)
    sim.check("enter: edit off", int("valueEdit" not in s), 1, absolute=0)
    sim.check("200k set", int(prop(sim, uid, "Resistance") == "200 kΩ"), 1, absolute=0,
              note=prop(sim, uid, "Resistance"))

    # The AZERTY number row without Shift (é = the 2 key, à = the 0 key, D10), and with Shift
    for keys, note in [(["é", "à", "à", "k"], "AZERTY unshifted"),
                       (["shift+2", "shift+0", "shift+0", "k"], "AZERTY shifted")]:
        sim.cli("circuit", "new", "--discard")
        sim.cli("ui", "click", "100,100")
        sim.cli("ui", "type", "r")
        uid = placed(sim)
        sim.cli("ui", "type", *keys, "enter")
        sim.check("%s digits: 200 kΩ" % note, int(prop(sim, uid, "Resistance") == "200 kΩ"), 1, absolute=0,
                  note=prop(sim, uid, "Resistance"))

    # Capacitors: multiplier u = µ, n; and the comma as the decimal separator
    for keys, want, note in [(["4", "u", "7"], "4.7 µF", "c 4u7"),
                             (["1", "0", "0", "n"], "100 nF", "c 100n"),
                             (["4", ",", "7", "u"], "4.7 µF", "c 4,7u")]:
        sim.cli("circuit", "new", "--discard")
        sim.cli("ui", "click", "100,100")
        sim.cli("ui", "type", "c")
        uid = placed(sim)
        sim.cli("ui", "type", *keys, "enter")
        sim.check("%s: %s" % (note, want), int(prop(sim, uid, "Capacitance") == want), 1, absolute=0,
                  note=prop(sim, uid, "Capacitance"))

    # Battery Voltage, Fixed Voltage Voltage (negative kept), WaveGen Freq
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "y")
    uid = placed(sim)
    sim.cli("ui", "type", "9", "enter")
    sim.check("battery 9 V", int(prop(sim, uid, "Voltage") == "9 V"), 1, absolute=0, note=prop(sim, uid, "Voltage"))
    sim.cli("ui", "type", "x")
    uid = placed(sim)
    sim.cli("ui", "type", "-", "5", "enter")
    sim.check("fixed voltage -5 V", int(prop(sim, uid, "Voltage") == "-5 V"), 1, absolute=0, note=prop(sim, uid, "Voltage"))
    sim.cli("ui", "type", "w")
    uid = placed(sim)
    sim.cli("ui", "type", "2", "k", "enter")
    sim.check("wavegen 2 kHz", int(prop(sim, uid, "Freq") == "2 kHz"), 1, absolute=0, note=prop(sim, uid, "Freq"))

    # No multiplier typed: the part's current one stays (4.7 kΩ, then 100 = 100 kΩ)
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    uid = placed(sim)
    sim.cli("ui", "type", "4", "k", "7", "enter")
    sim.check("4k7 set (unit k)", int(prop(sim, uid, "Resistance") == "4.7 kΩ"), 1, absolute=0,
              note=prop(sim, uid, "Resistance"))
    sim.cli("select", uid)
    sim.cli("ui", "type", "1", "0", "0", "enter")
    sim.check("digits only keep the k", int(prop(sim, uid, "Resistance") == "100 kΩ"), 1, absolute=0,
              note=prop(sim, uid, "Resistance"))

    # Capitals type multipliers too (K = k, U = u); M mega, m milli
    for keys, part, pname, want, note in [(["2", "0", "0", "K"], "r", "Resistance", "200 kΩ", "capital K"),
                                          (["4", "U", "7"], "c", "Capacitance", "4.7 µF", "capital U"),
                                          (["1", "M"], "r", "Resistance", "1 MΩ", "M mega"),
                                          (["1", "m"], "r", "Resistance", "1 mΩ", "m milli")]:
        sim.cli("circuit", "new", "--discard")
        sim.cli("ui", "click", "100,100")
        sim.cli("ui", "type", part)
        uid = placed(sim)
        sim.cli("ui", "type", *keys, "enter")
        sim.check("%s: %s" % (note, want), int(prop(sim, uid, pname) == want), 1, absolute=0,
                  note=prop(sim, uid, pname))

    # Backspace (D8): the buffer shrinks, an empty one ends the edit showing the old
    # value, and the next Backspace deletes the part
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    uid = placed(sim)
    s = sim.cli("ui", "type", "1", "2", "backspace")
    sim.check("backspace shrinks the buffer", int(s["valueEdit"]["buffer"] == "1"), 1, absolute=0)
    s = sim.cli("ui", "type", "backspace")
    sim.check("empty buffer ends the edit", int("valueEdit" not in s), 1, absolute=0)
    sim.check("still selected", int(s["selection"] == [uid]), 1, absolute=0)
    sim.check("old value shown", int(prop(sim, uid, "Resistance") == "100 Ω"), 1, absolute=0, note=prop(sim, uid, "Resistance"))
    sim.cli("ui", "type", "backspace")
    sim.check("the next backspace deletes the part", len(sim.cli("comp", "list")), 0, absolute=0)

    # Esc reverts and keeps the selection; Tab reverts and selects the next part
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    r1 = placed(sim)
    sim.cli("ui", "type", "r")                    # second part, right of the first
    r2 = placed(sim)
    sim.cli("select", r1)
    s = sim.cli("ui", "type", "5", "6", "esc")
    sim.check("esc: edit off", int("valueEdit" not in s), 1, absolute=0)
    sim.check("esc: still selected", int(s["selection"] == [r1]), 1, absolute=0)
    sim.check("esc: old value", int(prop(sim, r1, "Resistance") == "100 Ω"), 1, absolute=0, note=prop(sim, r1, "Resistance"))
    s = sim.cli("ui", "type", "5", "6", "tab")
    sim.check("tab: edit off", int("valueEdit" not in s), 1, absolute=0)
    sim.check("tab: next part selected", int(s["selection"] == [r2]), 1, absolute=0, note=str(s["selection"]))
    sim.check("tab: old value", int(prop(sim, r1, "Resistance") == "100 Ω"), 1, absolute=0, note=prop(sim, r1, "Resistance"))

    # Enter commits as one undo step
    sim.cli("select", r1)
    sim.cli("ui", "type", "4", "k", "7", "enter")
    sim.check("4k7 set", int(prop(sim, r1, "Resistance") == "4.7 kΩ"), 1, absolute=0, note=prop(sim, r1, "Resistance"))
    sim.cli("ui", "type", "cmd+z")
    sim.check("cmd+z restores the value", int(prop(sim, r1, "Resistance") == "100 Ω"), 1, absolute=0, note=prop(sim, r1, "Resistance"))

    # A scope (no numeric value) takes no typed value; neither do two selected parts
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "o")
    osc = placed(sim)
    s = sim.cli("ui", "type", "2", "5")
    sim.check("scope: no value edit", int("valueEdit" not in s), 1, absolute=0)
    sim.check("scope: still selected", int(s["selection"] == [osc]), 1, absolute=0)
    sim.cli("ui", "type", "r")
    r1 = placed(sim)
    sim.cli("select", r1, osc)
    s = sim.cli("ui", "type", "4")
    sim.check("two selected: no value edit", int("valueEdit" not in s), 1, absolute=0)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: typing a value", int(any(r["context"] == "One part selected"
                and r["keys"].startswith("0-9") for r in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
