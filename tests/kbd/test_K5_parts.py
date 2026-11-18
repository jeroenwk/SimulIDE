"""K5 (specs/keyboard-input.md): placing parts with single keys; switch keys A-Z.

Expected values follow the spec (decision D1 for u / q): each default letter places its part at
the keyboard cursor and selects it; with a part selected the new one is paired with it (nearest
pins); a switch Key is one capital A-Z, typed values become capital, anything else none; a
switch toggles on Shift+its letter; an older circuit's lowercase key does nothing and opening the
part's properties clears it. Relies on the user settings having no part shortcuts of their own
(the test reads `kbd shortcuts`-independent defaults through what is placed). Nothing is
simulated except the switch toggling.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402

KEYS = {
    "o": "Oscope", "a": "Amperimeter", "v": "Voltimeter", "f": "FreqMeter", "x": "Fixed Voltage",
    "y": "Battery", "w": "WaveGen", "g": "Ground", "b": "Push", "s": "Switch", "r": "Resistor",
    "p": "Potentiometer", "e": "elCapacitor", "c": "Capacitor", "i": "Inductor", "d": "Diode",
    "z": "Zener", "m": "Mosfet", "j": "Jfet", "t": "BJT", "q": "UJT", "u": "opAmp", "l": "Led",
}


def placed(sim, s):
    """(type, pos) of the one selected part."""
    if len(s["selection"]) != 1:
        return None, None
    g = sim.cli("comp", "get", s["selection"][0])
    return g["type"], g["pos"]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "mouse", "100,100")             # cross at (100, 100)
    wrong = []
    for key, typ in KEYS.items():
        s = sim.cli("ui", "type", key)
        t, p = placed(sim, s)
        if t != typ or p != [100, 100]:
            wrong.append("%s -> %s at %s" % (key, t, p))
        sim.cli("ui", "type", "Esc")
    sim.check("23 keys place their part at the cursor", len(KEYS) - len(wrong), len(KEYS), absolute=0,
              note="; ".join(wrong))

    # With R1 selected, r places R2 there, selected and paired (nearest pins)
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("select", "R1")
    sim.cli("ui", "mouse", "300,100")
    s = sim.cli("ui", "type", "r")
    t, p = placed(sim, s)
    sim.check("r: a resistor at the cursor", int(t == "Resistor" and p == [300, 100]), 1, absolute=0)
    sim.check("r: pinFrom R1.rPin", int(s["pinFrom"] == "R1.rPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("r: pinTo the new part's left pin", int((s["pinTo"] or "").endswith(".lPin")), 1, absolute=0,
              note=str(s["pinTo"]))
    sim.cli("ui", "type", "cmd+z")
    sim.check("Cmd+Z removes the placed part", len(sim.cli("comp", "list", "--type", "Resistor")), 1, absolute=0)

    # Switch keys: typed values
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Switch", "--label", "S1", "--at", "100,100")
    sim.cli("comp", "set", "S1", "Key=a")
    sim.check("Key=a becomes A", int(prop(sim, "S1", "Key") == "A"), 1, absolute=0, note=repr(prop(sim, "S1", "Key")))
    sim.cli("comp", "set", "S1", "Key=1")
    key = prop(sim, "S1", "Key")
    sim.check("Key=1 is refused (none)", int(key == ""), 1, absolute=0, note=repr(key))
    sim.cli("comp", "set", "S1", "Key=K")

    # Shift+K toggles it while running; k (no part bound) does nothing
    sim.cli("sim", "start")
    before = controls(sim, "S1")
    sim.cli("ui", "type", "k")
    mid = controls(sim, "S1")
    sim.cli("ui", "type", "K")
    after = controls(sim, "S1")
    sim.cli("sim", "stop")
    sim.check("k does not toggle", int(mid == before), 1, absolute=0)
    sim.check("Shift+K toggles", int(after != before), 1, absolute=0, note="%s -> %s" % (before, after))

    # An older circuit with Key="a": inert, cleared by opening the properties
    path = os.path.join(tempfile.mkdtemp(), "old_key.sim1")
    sim.cli("circuit", "save", path)
    with open(path) as f:
        text = f.read()
    with open(path, "w") as f:
        f.write(text.replace('Key="K"', 'Key="a"'))
    sim.cli("circuit", "open", path, "--discard")
    sim.check("old key loaded as is", int(prop(sim, "S1", "Key") == "a"), 1, absolute=0, note=repr(prop(sim, "S1", "Key")))
    sim.cli("ui", "mouse", "300,300")
    s = sim.cli("ui", "type", "a")
    t, _ = placed(sim, s)
    sim.check("a places an ammeter (not the old switch key)", int(t == "Amperimeter"), 1, absolute=0, note=str(t))
    sim.cli("view", "props", "S1")
    sim.check("opening the properties clears it", int(prop(sim, "S1", "Key") == ""), 1, absolute=0,
              note=repr(prop(sim, "S1", "Key")))

    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: part letters", int(any(r["keys"] == "its letter" for r in sc)), 1, absolute=0)


def prop(sim, comp, name):
    props = sim.cli("comp", "get", comp, "--all")["properties"]
    if isinstance(props, dict):
        v = props.get(name)
        return v.get("value") if isinstance(v, dict) else v
    for p in props:
        if p.get("name") == name:
            return p.get("value")
    return None


def controls(sim, comp):
    return str(sim.cli("ui", "controls", comp))


if __name__ == "__main__":
    run_test(test)
