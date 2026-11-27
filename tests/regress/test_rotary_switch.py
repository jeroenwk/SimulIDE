"""Rotary switch part (docs/ROTARY_SWITCH.md): the contacts in every position, 1 to 4 poles,
3 to 12 positions and their limits, the throws clockwise, the contact resistance, the timing of a
step (break before make, make before break, bounce), the knob (click, dial), save and reopen,
going back in time.

Expected values are Ohm's law on the test circuit (5 V through 100 Ohm into the common, 1 kOhm
from every throw to ground: the made throw sits at 5 x 1000 / 1100 = 4.5455 V, the others at
0 V), the part's definitions (30 deg per detent, poles x positions at most 12 as on the SR25 /
SR26 deck, the gap and bounce counts the Switch has, create-circuit #136) and the Alpha SR26
datasheet (contact resistance 50 mOhm max.). Results are [SIMULATED].

    python3 tests/regress/test_rotary_switch.py              # every section
    python3 tests/regress/test_rotary_switch.py timing back  # some sections
"""

import os
import sys
import tempfile

from harness import run_test, SimError

SECTIONS = sys.argv[1:]


def made_volts(poles, rc=0.0):
    """The made throws with the commons on one 100 Ohm: they load it together."""
    return 5.0 * 1000 / (100 * poles + 1000 + rc * poles)
POLES = "ABCD"


def want(name):
    return not SECTIONS or name in SECTIONS


def throws(poles, positions):
    return [POLES[p] + str(k + 1) for p in range(poles) for k in range(positions)]


def build(sim, poles=1, positions=4, **props):
    """5 V - 100 Ohm - every common (A, B, C, D) ; every throw - 1 kOhm - ground."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-176,0", "Voltage=5")
    c("comp", "add", "Resistor", "--label", "RS", "--at", "-128,0", "Resistance=100")
    settings = {"Poles": poles, "Positions": positions}
    settings.update(props)
    c("comp", "add", "RotarySwitch", "--label", "SW", "--at", "0,0", *["%s=%s" % kv for kv in settings.items()])
    c("comp", "add", "Ground", "--label", "G", "--at", "-176,80")
    c("wire", "add", "B1.lPin", "RS.lPin")
    c("wire", "add", "B1.rPin", "G.Gnd")
    for pole in POLES[:poles]:                      # The commons share the 100 Ohm
        c("wire", "add", "RS.rPin", "SW." + pole)
    for i, t in enumerate(throws(poles, positions)):
        c("comp", "add", "Resistor", "--label", "R" + t, "--at", "%d,%d" % (160 + 40 * (i % 6), 160 + 32 * (i // 6)), "Resistance=1k")
        c("wire", "add", "SW." + t, "R" + t + ".lPin")
        c("wire", "add", "R" + t + ".rPin", "G.Gnd")
    c("sim", "run", "--for", "1ms")


def volts(sim, pins):
    return [abs(sim.cli("probe", *pins)[i]["voltage"]) for i in range(len(pins))]


def check_throws(sim, where, poles, positions, made, rc=0.0):
    """Throw `made` (1..positions) of every pole is made, the others are open."""
    high = made_volts(poles, rc)
    names = throws(poles, positions)
    v = volts(sim, ["SW." + t for t in names])
    ok = all(abs(v[i] - (high if (i % positions) == made - 1 else 0)) < 2e-3 for i in range(len(names)))
    sim.check("%s: throw %d of each pole made, the others open" % (where, made), int(ok), 1, absolute=0)


def flag(sim, name, cond):
    sim.check(name, 1 if cond else 0, 1, absolute=0)


def pins_of(sim):
    return [p["label"] for p in sim.cli("comp", "get", "SW")["pins"]]


def prop(sim, name):
    for p in sim.cli("comp", "get", "SW")["properties"]:
        if p["name"] == name:
            return p["value"]
    raise KeyError(name)


def section_contacts(sim):
    build(sim)
    flag(sim, "default 1 pole x 4: pins A, A1..A4", pins_of(sim) == ["A", "A1", "A2", "A3", "A4"])
    for n in range(1, 5):
        sim.cli("ui", "dial", "SW", str(n - 1))
        sim.cli("sim", "run", "--for", "1ms")
        check_throws(sim, "position %d" % n, 1, 4, n)
        flag(sim, "position %d shown as Position" % n, prop(sim, "Position") == str(n))

    # The knob: a click on it is one detent clockwise; at the last position it stops
    sim.cli("ui", "dial", "SW", "0")
    sim.cli("sim", "run", "--for", "1ms")
    sim.cli("ui", "click", "0,0")                     # The knob is where the part is
    sim.cli("sim", "run", "--for", "1ms")
    check_throws(sim, "a click on the knob", 1, 4, 2)
    for _ in range(5):
        sim.cli("ui", "click", "0,0")
    sim.cli("sim", "run", "--for", "1ms")
    check_throws(sim, "five more clicks", 1, 4, 4)

    # The dial has one value per position and refuses the others
    dial = sim.cli("ui", "controls", "SW")["controls"][0]
    flag(sim, "the knob is the part's dial, 0..positions-1", dial["kind"] == "dial" and dial["min"] == 0 and dial["max"] == 3)
    try:
        sim.cli("ui", "dial", "SW", "4")
        refused = False
    except SimError:
        refused = True
    flag(sim, "ui dial past the last position is refused", refused)
    check_throws(sim, "after the refused value", 1, 4, 4)

    # Position, set as a property
    sim.cli("comp", "set", "SW", "Position=2")
    sim.cli("sim", "run", "--for", "1ms")
    check_throws(sim, "Position=2", 1, 4, 2)

    # Turned and flipped: the wires follow, the contacts are the same
    sim.cli("comp", "rotate", "SW", "cw")
    sim.cli("comp", "flip", "SW", "h")
    sim.cli("sim", "run", "--for", "1ms")
    check_throws(sim, "rotated and flipped", 1, 4, 2)


def section_poles(sim):
    for poles, positions in [(2, 6), (3, 4), (4, 3), (1, 12), (1, 3)]:
        build(sim, poles, positions)
        flag(sim, "%d x %d: pins" % (poles, positions), pins_of(sim) == list(POLES[:poles]) + throws(poles, positions))
        for n in sorted({1, (positions + 1) // 2, positions}):
            sim.cli("ui", "dial", "SW", str(n - 1))
            sim.cli("sim", "run", "--for", "1ms")
            check_throws(sim, "%d x %d, position %d" % (poles, positions, n), poles, positions, n)


def section_limits(sim):
    build(sim, 1, 4)
    for poles, positions, got in [(2, 12, (2, 6)), (4, 4, (4, 3)), (1, 2, (1, 3)), (1, 20, (1, 12)),
                                  (9, 3, (4, 3)), (0, 3, (1, 3)), (3, 12, (3, 4)), (1, 3, (1, 3))]:
        sim.cli("comp", "set", "SW", "Poles=%d" % poles, "Positions=%d" % positions)
        sim.cli("comp", "set", "SW", "Poles=%d" % poles, "Positions=%d" % positions)   # again: now from the clamped state
        flag(sim, "Poles=%d, Positions=%d give %d x %d" % (poles, positions, got[0], got[1]),
             (int(prop(sim, "Poles")), int(prop(sim, "Positions"))) == got and len(pins_of(sim)) == got[0] * (got[1] + 1))
    build(sim, 3, 4)
    flag(sim, "3 x 4 after the limits", pins_of(sim) == ["A", "B", "C"] + throws(3, 4))

    # Fewer positions than the knob stood: it comes back inside
    sim.cli("ui", "dial", "SW", "3")                  # position 4
    sim.cli("sim", "run", "--for", "1ms")
    sim.cli("sim", "stop")
    sim.cli("comp", "set", "SW", "Positions=3")
    flag(sim, "Positions 3 with the knob at 4: Position is 3", prop(sim, "Position") == "3")
    flag(sim, "Positions 3: the pins", pins_of(sim) == ["A", "B", "C"] + throws(3, 3))
    build(sim, 3, 3, Position=3)                    # wired again: the wires of the gone pins went with them
    check_throws(sim, "3 x 3, position 3", 3, 3, 3)


def section_resistance(sim):
    # Closed contact 50 mOhm (default) and 500 mOhm: the common minus the made throw is I x Rc
    build(sim, 1, 4, Position=3)
    for rc_text, rc in [(None, 0.05), ("500 mΩ", 0.5)]:
        if rc_text:
            sim.cli("comp", "set", "SW", "Contact_Res=" + rc_text)
        sim.cli("sim", "run", "--for", "1ms")
        current = 5.0 / (100 + 1000 + rc)
        va = abs(sim.cli("probe", "SW.A")[0]["voltage"])
        vt = abs(volts(sim, ["SW.A3"])[0])
        sim.check("Rc %s: drop over the contact (mV)" % rc, 1e3 * (va - vt), 1e3 * current * rc, tol=0.02)
        sim.check("Rc %s: the made throw (V)" % rc, vt, 5.0 * 1000 / (100 + 1000 + rc), tol=0.002)
    flag(sim, "Contact_Res saved as 500 mOhm", prop(sim, "Contact_Res").split()[0] == "500")


def section_timing(sim):
    c = sim.cli

    # Break before make (the default): 2 ms with nothing connected
    build(sim, Transit_Time="2ms")
    c("ui", "dial", "SW", "1")
    samples = sample(sim, 40, "100us", ["SW.A1", "SW.A2"])
    low = [s for s in samples if s[0] < 1 and s[1] < 1]
    both = [s for s in samples if s[0] > 1 and s[1] > 1]
    flag(sim, "not shorting: never both throws connected", not both)
    flag(sim, "not shorting: the old throw opens at once (%d of the first 19 samples still high)" % sum(1 for s in samples[:19] if s[0] > 1),
         sum(1 for s in samples[:19] if s[0] > 1) <= 1)
    flag(sim, "not shorting: nothing connected for about 2 ms (%d samples)" % len(low), 17 <= len(low) <= 21)
    flag(sim, "not shorting: the new throw is made at the end", samples[-1][1] > 1 and samples[-1][0] < 1)

    # Make before break: 2 ms with both connected
    build(sim, Shorting="true", Overlap_Time="2ms")
    c("ui", "dial", "SW", "1")
    samples = sample(sim, 40, "100us", ["SW.A1", "SW.A2"])
    both = [s for s in samples if s[0] > 1 and s[1] > 1]
    low = [s for s in samples if s[0] < 1 and s[1] < 1]
    flag(sim, "shorting: never nothing connected", not low)
    flag(sim, "shorting: both throws connected for about 2 ms (%d samples)" % len(both), 17 <= len(both) <= 21)
    flag(sim, "shorting: the old throw opens at the end", samples[-1][1] > 1 and samples[-1][0] < 1)

    # Bounce at the make (not shorting): 2 x 4 changes after the make, within 2 ms, ends closed
    build(sim, Bounce_Time="2ms", Bounces=4)
    c("ui", "dial", "SW", "1")
    changes, last, high = burst(sim, "SW.A2", before=False)
    sim.check("not shorting, bounce: changes of the new throw", changes, 9, absolute=0)
    sim.check("not shorting, bounce: last change within 2 ms (us)", min(last, 2020), 2020, absolute=2000)
    flag(sim, "not shorting, bounce: ends closed", high)
    c("ui", "dial", "SW", "2")
    c("sim", "run", "--for", "3ms")
    check_throws(sim, "bounce, next step", 1, 4, 3)

    # Bounce at the break (shorting): 2 x 4 + 1 changes of the old throw, ends open
    build(sim, Shorting="true", Overlap_Time="0", Bounce_Time="2ms", Bounces=4)
    c("ui", "dial", "SW", "1")
    changes, last, high = burst(sim, "SW.A1", before=True)
    sim.check("shorting, bounce: changes of the old throw", changes, 9, absolute=0)
    sim.check("shorting, bounce: last change within 2 ms (us)", min(last, 2020), 2020, absolute=2000)
    flag(sim, "shorting, bounce: ends open", not high)
    flag(sim, "shorting, bounce: the new throw was made at once", volts(sim, ["SW.A2"])[0] > 1)

    # Two steps quickly: the second cuts the first short, and ends where the knob is
    build(sim, Transit_Time="5ms")
    c("ui", "dial", "SW", "1")
    c("sim", "run", "--for", "1ms")
    c("ui", "dial", "SW", "2")
    c("sim", "run", "--for", "1ms")
    v = volts(sim, ["SW.A1", "SW.A2", "SW.A3"])
    flag(sim, "two quick steps: nothing connected during the second transit", max(v) < 1)
    c("sim", "run", "--for", "5ms")
    check_throws(sim, "two quick steps, after the second transit", 1, 4, 3)

    # Two steps quickly, make before break: the second cuts the first overlap short. The contact
    # made last (throw 2) is the one that opens after the second overlap; the older one (throw 1,
    # still closed from the first step) opens at once
    build(sim, Shorting="true", Overlap_Time="5ms")
    c("ui", "dial", "SW", "1")
    c("sim", "run", "--for", "1ms")
    v = volts(sim, ["SW.A1", "SW.A2", "SW.A3"])
    flag(sim, "shorting, two quick steps: 1 and 2 made in the first overlap", v[0] > 1 and v[1] > 1 and v[2] < 1)
    c("ui", "dial", "SW", "2")
    c("sim", "run", "--for", "10us")
    v = volts(sim, ["SW.A1", "SW.A2", "SW.A3"])
    flag(sim, "shorting, two quick steps: the older contact opens at once, the last made one waits",
         v[0] < 1 and v[1] > 1 and v[2] > 1)
    c("sim", "run", "--for", "6ms")
    check_throws(sim, "shorting, two quick steps, after the second overlap", 1, 4, 3)


def sample(sim, n, dt, pins):
    out = []
    for _ in range(n):
        sim.cli("sim", "run", "--for", dt)
        out.append(volts(sim, pins))
    return out


def burst(sim, pin, before, ms=3.0, dt_us=20):
    """`pin` sampled every dt_us for ms: (changes, time of the last one in us, high at the end)."""
    was = volts(sim, [pin])[0] > 2.5
    v, t = [], []
    for i in range(int(ms * 1000 / dt_us)):
        sim.cli("sim", "run", "--for", "%dus" % dt_us)
        v.append(volts(sim, [pin])[0] > 2.5)
        t.append((i + 1) * dt_us)
    changes = [t[i] for i in range(1, len(v)) if v[i] != v[i - 1]]
    first = 1 if v[0] != was else 0            # the change before the first sample
    return first + len(changes), (changes[-1] if changes else 0), v[-1]


def section_save(sim):
    build(sim, 3, 4, Position=3, Shorting="true", Overlap_Time="3ms", Bounce_Time="1ms", Bounces=7, Contact_Res="200 mΩ", Key="S")
    before = pins_of(sim)
    fd, path = tempfile.mkstemp(suffix=".sim1")
    os.close(fd)
    try:
        sim.cli("sim", "stop")
        sim.cli("circuit", "save", path)
        sim.cli("circuit", "open", path, "--discard")
        sim.cli("sim", "run", "--for", "1ms")
        flag(sim, "reopened: the pins", pins_of(sim) == before)
        for name, value in [("Poles", "3"), ("Positions", "4"), ("Position", "3"), ("Shorting", "true"),
                            ("Overlap_Time", "3 ms"), ("Bounce_Time", "1 ms"), ("Bounces", "7"), ("Key", "S")]:
            flag(sim, "reopened: %s is %s" % (name, value), prop(sim, name) == value)
        flag(sim, "reopened: Contact_Res is 200 mOhm", prop(sim, "Contact_Res").split()[0] == "200")
        check_throws(sim, "reopened: position 3", 3, 4, 3, rc=0.2)
        flag(sim, "reopened: the knob shows the position", sim.cli("ui", "controls", "SW")["controls"][0]["value"] == 2)

        # The key acts on release, as the Switch: the press alone does not turn it
        sim.cli("ui", "key", "S", "down")
        sim.cli("sim", "run", "--for", "1ms")
        flag(sim, "Shift+S pressed: no turn yet", prop(sim, "Position") == "3")
        sim.cli("ui", "key", "S", "up")
        sim.cli("sim", "run", "--for", "1ms")
        v = volts(sim, ["SW.A3", "SW.A4", "SW.C3", "SW.C4"])
        flag(sim, "Shift+S released: the next throws are made (with the old ones, inside the overlap)", v[1] > 1 and v[3] > 1)
        sim.cli("sim", "run", "--for", "5ms")
        check_throws(sim, "Shift+S released, after the overlap", 3, 4, 4, rc=0.2)
    finally:
        try:
            os.remove(path)
        except OSError:
            pass


def section_back(sim):
    # State code (specs/sim-control.md §4.8): the history is usable with the part, and going back
    # gives the same state again, with a step under way
    c = sim.cli
    build(sim, 2, 6, Transit_Time="1ms", Shorting="true", Overlap_Time="0.5ms", Bounce_Time="0.2ms", Bounces=3)
    h = c("sim", "history")
    flag(sim, "the part has state code (the history is usable)", bool(h["usable"]))
    c("ui", "dial", "SW", "2")                     # position 3 while running: a step with all the timing
    for _ in range(6):
        c("sim", "skip", "1ms", timeout=60)
    k = c("sim", "history", "check", "--span", "1ms", "--points", "3", "--ago", "4ms", timeout=300)
    flag(sim, "identical after going back", bool(k["ok"]))

    # Saved key frames: before and after a step, the contacts and the knob
    fd, path = tempfile.mkstemp(suffix=".sim1")
    os.close(fd)
    try:
        c("sim", "stop")
        c("comp", "set", "SW", "Position=2")      # Back from the step of the history check
        c("circuit", "save", path)                # Key frames are kept next to the circuit's file
        c("sim", "run", "--for", "1ms")
        c("sim", "keyframe", "save", "--note", "position 2")
        c("ui", "dial", "SW", "3")
        c("sim", "run", "--for", "1ms")
        c("sim", "keyframe", "save", "--note", "position 4")
        c("sim", "keyframe", "open", "1")
        check_throws(sim, "key frame 1: position 2", 2, 6, 2)
        flag(sim, "key frame 1: the knob is at position 2", c("ui", "controls", "SW")["controls"][0]["value"] == 1)
        c("sim", "keyframe", "open", "2")
        check_throws(sim, "key frame 2: position 4", 2, 6, 4)
        flag(sim, "key frame 2: the knob is at position 4", c("ui", "controls", "SW")["controls"][0]["value"] == 3)
    finally:
        c("sim", "stop")
        import shutil
        try:
            os.remove(path)
            shutil.rmtree(path[:-len(".sim1")] + ".keyframes")   # The folder of its key frames
        except OSError:
            pass


def section_looks(sim):
    # Turned and flipped, in settings with 3 and with 4 pins on a side: the knob widget stays in
    # the middle of the body, the pins stay on the 8 px grid, the contacts are the same. (That
    # the knob is upright and the names horizontal is checked on captures: docs/ROTARY_SWITCH.md)
    c = sim.cli
    for poles, positions, width in [(1, 4, 80), (2, 6, 96)]:
        for turn in [None, "cw", "180", "ccw"]:
            for flip in [None, "h", "v"]:
                build(sim, poles, positions)
                if turn:
                    c("comp", "rotate", "SW", turn)
                if flip:
                    c("comp", "flip", "SW", flip)
                g = c("comp", "get", "SW")
                where = "%d x %d, %s%s" % (poles, positions, turn or "not turned", ", flipped " + flip if flip else "")
                x, y, w, h = g["controls"][0]
                off = max(abs(x + w / 2 - g["pos"][0]), abs(y + h / 2 - g["pos"][1]))
                flag(sim, where + ": the knob in the middle of the body (%.0f px off)" % off, off <= 1)
                flag(sim, where + ": the pins on the 8 px grid",
                     all(v % 8 == 0 for pin in g["pins"] for v in pin["pos"]))
                flag(sim, where + ": body %d px wide" % width, g["body"][2] == width and g["body"][3] == width)
                c("sim", "run", "--for", "1ms")
                check_throws(sim, where, poles, positions, 1)


def section_clockwise(sim):
    # The throws run clockwise, as the pins are numbered on the deck seen from its pins: along the
    # top from left to right, down the right side, then along the bottom from right to left. The
    # commons stay on the left
    c = sim.cli
    for poles, positions in [(1, 3), (1, 4), (1, 5), (2, 3), (2, 4), (2, 5), (1, 12), (2, 6), (3, 4), (4, 3)]:
        c("circuit", "new", "--discard")
        c("comp", "add", "RotarySwitch", "--label", "SW", "--at", "0,0", "Poles=%d" % poles, "Positions=%d" % positions)
        pos = {p["label"]: p["pos"] for p in c("comp", "get", "SW")["pins"] if p["label"] in throws(poles, positions)}
        on = {s: [t for t in pos if (s == "top" and pos[t][1] < -24 or s == "bottom" and pos[t][1] > 24
                                      or s == "right" and pos[t][0] > 24)] for s in ("top", "right", "bottom")}
        top = sorted(on["top"], key=lambda t: pos[t][0])
        right = sorted(on["right"], key=lambda t: pos[t][1])
        bottom = sorted(on["bottom"], key=lambda t: pos[t][0], reverse=True)
        flag(sim, "%d x %d: the throws clockwise: top, right, bottom from the right" % (poles, positions),
             top + right + bottom == throws(poles, positions) and len(top) + len(right) + len(bottom) == poles * positions)


def section_turn(sim):
    # setAngle() sets the angle, it does not turn by it: undo and redo of a turn, `comp set` of the
    # rotation on a turned part, and a reopened file all give the angle asked for, with the knob in
    # the middle of the body (the part is at 0,0 there)
    c = sim.cli
    build(sim)

    def state():
        g = c("comp", "get", "SW")
        return g.get("rotation", 0), g["pins"][1]["pos"], g["controls"][0]

    def centred(knob):
        x, y, w, h = knob
        return abs(x + w / 2) <= 1 and abs(y + h / 2) <= 1

    pin0, knob = state()[1], state()[2]
    c("comp", "rotate", "SW", "cw")
    pin90 = state()[1]
    flag(sim, "turn: rotate cw gives 90 deg, the knob in the middle", state()[0] == 90 and pin90 != pin0 and centred(state()[2]))
    c("undo")
    flag(sim, "turn: undo gives 0 deg again, pins and knob back",
         state()[0] == 0 and state()[1] == pin0 and centred(state()[2]))
    c("redo")
    flag(sim, "turn: redo gives 90 deg again", state()[0] == 90 and state()[1] == pin90 and centred(state()[2]))
    c("comp", "set", "SW", "rotation=90")
    flag(sim, "turn: the rotation set to 90 on a part at 90 stays there", state()[0] == 90 and state()[1] == pin90)
    c("comp", "set", "SW", "rotation=0")
    flag(sim, "turn: the rotation set to 0 turns a turned part back",
         state()[0] == 0 and state()[1] == pin0 and centred(state()[2]))
    c("comp", "rotate", "SW", "cw")
    fd, path = tempfile.mkstemp(suffix=".sim1")
    os.close(fd)
    try:
        c("sim", "stop")
        c("circuit", "save", path)
        c("circuit", "open", path, "--discard")
        flag(sim, "turn: a turned part reopens at 90 deg, the knob in the middle",
             state()[0] == 90 and state()[1] == pin90 and centred(state()[2]))
    finally:
        try:
            os.remove(path)
        except OSError:
            pass


def test(sim):
    for name, fn in [("contacts", section_contacts), ("poles", section_poles), ("limits", section_limits),
                     ("resistance", section_resistance), ("timing", section_timing), ("save", section_save),
                     ("back", section_back), ("turn", section_turn), ("clockwise", section_clockwise), ("looks", section_looks)]:
        if want(name):
            fn(sim)


if __name__ == "__main__":
    run_test(test)
