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
    "y": "Battery", "n": "WaveGen", "g": "Ground", "b": "Push", "s": "Switch", "r": "Resistor",
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
    sim.cli("ui", "click", "100,100")             # cross at (100, 100)
    wrong = []
    for key, typ in KEYS.items():
        s = sim.cli("ui", "type", key)
        t, p = placed(sim, s)
        if t != typ or p != [100, 100]:
            wrong.append("%s -> %s at %s" % (key, t, p))
        sim.cli("ui", "type", "Esc")
    sim.check("23 keys place their part at the cursor", len(KEYS) - len(wrong), len(KEYS), absolute=0,
              note="; ".join(wrong))

    # With R1 selected, r places R2 there, selected and paired (nearest pins):
    # R1's selected pin rPin (116,100) points right, so R2's first pin lPin goes
    # 64 px beyond it (180,100), unrotated, in a straight line (D17)
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "r")
    t, p = placed(sim, s)
    sim.check("r with R1 selected: R2 at 196,100 (lPin at 180,100)", int(t == "Resistor" and p == [196, 100]), 1, absolute=0,
              note=str(p))
    pins = sim.cli("comp", "get", s["selection"][0])["pins"]
    sim.check("r: the new part's lPin faces R1.rPin at 64 px", int(pins[0]["pos"] == [180, 100]), 1, absolute=0,
              note=str(pins))
    sim.check("r: pinFrom R1.rPin", int(s["pinFrom"] == "R1.rPin"), 1, absolute=0, note=str(s["pinFrom"]))
    sim.check("r: pinTo the new part's left pin", int((s["pinTo"] or "").endswith(".lPin")), 1, absolute=0,
              note=str(s["pinTo"]))
    sim.cli("ui", "type", "cmd+z")
    sim.check("Cmd+Z removes the placed part", len(sim.cli("comp", "list", "--type", "Resistor")), 1, absolute=0)

    # The same from a pin pointing down: R1 rotated (Cmd+R), its rPin now points
    # down, so c places a capacitor below with its lPin pointing up at it (D17)
    sim.cli("select", "R1")
    sim.cli("ui", "type", "cmd+r")
    s = sim.cli("ui", "type", "c")
    uid = s["selection"][0]
    pins = sim.cli("comp", "get", uid)["pins"]
    sim.check("c below the pin: lPin at 100,180 pointing up", int(pins[0]["pos"] == [100, 180]), 1, absolute=0,
              note=str(pins))
    pos = sim.cli("comp", "get", uid)["pos"]
    sim.check("c below the pin: straight below R1's rPin", int(pos == [100, 196]), 1, absolute=0, note=str(pos))

    # Placing to the left flips instead of rotating 180° (D17): with R1's lPin
    # selected (outgoing left), r lands left of it, lPin pointing right, and the
    # label upright — the saved circuit shows the transform: hflip, rotation 0
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "200,100")
    sim.cli("select", "R1")
    sim.cli("ui", "type", ">")                      # Selected pin: lPin, pointing left
    s = sim.cli("ui", "type", "r")
    uid = s["selection"][0]
    r2 = sim.cli("comp", "get", uid)
    sim.check("left: lPin points right at R1's lPin",
              int([p for p in r2["pins"] if p["id"] == "lPin"][0]["side"] == "right"), 1, absolute=0,
              note=str([p["side"] for p in r2["pins"]]))
    import tempfile
    path = os.path.join(tempfile.mkdtemp(), "leftplace.sim1")
    sim.cli("circuit", "save", path)
    xml = [l for l in open(path).read().split("><") if 'CircId="%s"' % uid in l]
    sim.check("left: a flip, not a rotation", int(any('hflip="-1"' in l and 'rotation="0"' in l for l in xml)), 1,
              absolute=0, note=str(xml)[:120])

    # The new part's selected pin is the one nearest to the outgoing pin (D18):
    # the BJT placed after R1 has its collector (the first pin, facing R1) selected
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    s = sim.cli("ui", "type", "t")
    bjt = s["selection"][0]
    sim.check("the new part's selected pin faces the outgoing pin",
              int(s["selectedPin"] == bjt+".collector"), 1, absolute=0, note=str(s.get("selectedPin")))
    sim.check("pinFrom is the outgoing pin", int(s["pinFrom"] == "Resistor-1.rPin"), 1, absolute=0,
              note=str(s["pinFrom"]))

    # Enter remembers the pin it left from: a part placed while the cross is still
    # there continues the chain, also with nothing selected (D17). A transistor
    # with its collector (first pin, pointing up) selected, Enter, then r
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    s = sim.cli("ui", "type", "t")                   # BJT, selected pin = collector (top)
    bjt = s["selection"][0]
    s = sim.cli("ui", "type", "enter")
    col = sim.cli("comp", "get", bjt)["pins"]
    col = [p for p in col if p["id"] == "collector"][0]["pos"]
    sim.check("enter: cross beyond the collector", int(s["cursor"] == [col[0], col[1]-64]), 1, absolute=0,
              note=str(s["cursor"]))
    s = sim.cli("ui", "type", "r")
    uid = s["selection"][0]
    pins = sim.cli("comp", "get", uid)["pins"]
    sim.check("r after enter: lPin on the collector's pin, pointing down",
              int(pins[0]["pos"] == [col[0], col[1]-64]), 1, absolute=0, note=str(pins))

    # A cross moved after Enter places the part centred at the cross, unrotated
    sim.cli("circuit", "new", "--discard")
    sim.cli("ui", "click", "100,100")
    sim.cli("ui", "type", "r")
    sim.cli("ui", "type", "enter")
    sim.cli("ui", "type", "cmd+Right")               # The cross moves away (64 px, D23)
    s = sim.cli("ui", "type", "r")
    uid = s["selection"][0]
    r2 = sim.cli("comp", "get", uid)
    sim.check("cross moved: part centred at the cross", int(r2["pos"] == [244, 100]), 1, absolute=0,
              note=str(r2["pos"]))
    sim.check("cross moved: part unrotated", int(r2["pins"][1]["pos"][0] - r2["pins"][0]["pos"][0] == 32), 1, absolute=0,
              note=str([p["pos"] for p in r2["pins"]]))

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
    sim.cli("ui", "click", "300,300")
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
