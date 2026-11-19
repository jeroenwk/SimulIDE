"""Wire colours and selecting several wires (specs/wire-colors.md).

Expected values follow the spec: in network mode Enter marks / unmarks the current
wire, * marks the walk's whole net (again: unmarks it), Cmd+click toggles wires
(the current one marked first), Cmd+double-click marks a wire's net, Backspace
deletes the marked wires in one undo step; leaving network mode drops the marks.
Colours (line, band, band width) are per wire, set as one undo step, saved in the
.sim1 and kept by both halves of a split wire. Nothing is simulated.
"""

import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def net(s):
    return s.get("network")


def marked(s):
    n = net(s)
    return sorted(n["marked"]) if n else None


def wires(sim):
    return {w["uid"]: w for w in sim.cli("wire", "list")}


def middle(w):
    """A point in the middle of the wire's longest segment (away from its corners)."""
    p = list(zip(w["points"][0::2], w["points"][1::2]))
    a, b = max(zip(p, p[1:]), key=lambda s: abs(s[0][0] - s[1][0]) + abs(s[0][1] - s[1][1]))
    return "%d,%d" % ((a[0] + b[0]) // 2, (a[1] + b[1]) // 2)


def build(sim):
    """K9's circuit (R1, R2 to a dot on R1's wire to R3) plus R3 to R4: two nets."""
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "100,200")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "300,148")
    sim.cli("comp", "add", "Resistor", "--label", "R4", "--at", "460,148")
    sim.cli("wire", "add", "R1.rPin", "R3.lPin")
    sim.cli("wire", "add", "R2.rPin", "R1.rPin")       # A dot on R1's wire
    sim.cli("wire", "add", "R3.rPin", "R4.lPin")


def test(sim):
    build(sim)
    ws = wires(sim)
    net_b = [u for u, w in ws.items() if "R4" in w.get("to", "") + w.get("from", "")]
    net_a = sorted(u for u in ws if u not in net_b)
    sim.check("circuit: 3 wires in net A, 1 in net B", int(len(net_a) == 3 and len(net_b) == 1), 1,
              absolute=0, note=str(ws.keys()))

    # Keyboard: = Enter marks, Tab to another wire, Enter marks it too
    sim.cli("select", "R1")
    s = sim.cli("ui", "type", "=")
    sim.check("=: nothing marked", int(marked(s) == []), 1, absolute=0, note=str(marked(s)))
    first = net(s)["step"]["uid"]
    s = sim.cli("ui", "type", "Enter")
    sim.check("Enter: the current wire marked", int(marked(s) == [first]), 1, absolute=0, note=str(marked(s)))
    sim.check("Enter: network mode kept", int(net(s) is not None), 1, absolute=0)
    for _ in range(10):
        s = sim.cli("ui", "type", "tab")
        st = net(s)["step"]
        if st["kind"] == "wire" and st["uid"] != first:
            break
    second = net(s)["step"]["uid"]
    sim.check("Tab: marks kept", int(marked(s) == [first]), 1, absolute=0, note=str(marked(s)))
    s = sim.cli("ui", "type", "Enter")
    sim.check("Enter on a second wire: two marked", int(marked(s) == sorted([first, second])), 1,
              absolute=0, note=str(marked(s)))
    sim.check("marked wires selected in the scene", int(sorted(net(s)["selectedWires"]) == sorted([first, second])),
              1, absolute=0, note=str(net(s)["selectedWires"]))
    s = sim.cli("ui", "type", "Enter")
    sim.check("Enter again: unmarked", int(marked(s) == [first]), 1, absolute=0, note=str(marked(s)))

    # *: the whole net, again: none
    s = sim.cli("ui", "type", "*")
    sim.check("*: the net's 3 wires marked", int(marked(s) == net_a), 1, absolute=0, note=str(marked(s)))
    s = sim.cli("ui", "type", "*")
    sim.check("* again: none marked", int(marked(s) == []), 1, absolute=0, note=str(marked(s)))

    # Space opens the dialog (headless: closed at once, nothing changed)
    sim.cli("ui", "type", "*")
    sim.cli("ui", "type", "space")
    s = sim.cli("kbd", "state")
    sim.check("space: still network mode, marks kept", int(marked(s) == net_a), 1, absolute=0, note=str(marked(s)))
    sim.check("space, cancelled: no colours", int(all("color" not in w for w in wires(sim).values())), 1, absolute=0)

    s = sim.cli("ui", "type", "Escape")
    sim.check("Esc: network mode left", int(net(s) is None), 1, absolute=0)

    # Mouse: Cmd+click toggles, across nets
    a0 = ws[net_a[0]]
    s = sim.cli("ui", "click", middle(a0), "--cmd")
    cur = net(s)["step"]["uid"]
    sim.check("cmd+click: network mode, nothing marked", int(marked(s) == [] and cur == net_a[0]), 1,
              absolute=0, note="%s %s" % (cur, marked(s)))
    s = sim.cli("ui", "click", middle(ws[net_b[0]]), "--cmd")
    sim.check("cmd+click a second wire (other net): both marked", int(marked(s) == sorted([net_a[0], net_b[0]])),
              1, absolute=0, note=str(marked(s)))
    s = sim.cli("ui", "click", middle(ws[net_b[0]]), "--cmd")
    sim.check("cmd+click it again: unmarked", int(marked(s) == [net_a[0]]), 1, absolute=0, note=str(marked(s)))
    s = sim.cli("ui", "click", "400,320")
    sim.check("click on the canvas: network mode left", int(net(s) is None), 1, absolute=0)

    # Cmd+double-click: the net
    s = sim.cli("ui", "click", middle(ws[net_a[1]]), "--cmd", "--double")
    sim.check("cmd+double-click: the net marked", int(marked(s) == net_a), 1, absolute=0, note=str(marked(s)))
    sim.cli("ui", "type", "Escape")

    # Instrument leads on the net are left out (W5), by the mouse and by *: a voltmeter
    # lead to a dot on R2's wire, and a scope channel joined to that lead at a second
    # dot, so the stretch between the two dots only leads to instruments too
    old_nodes = {e.split(".")[0] for w in ws.values() for e in (w.get("from", ""), w.get("to", ""))
                 if e.startswith("Node")}
    sim.cli("comp", "add", "Voltmeter", "--label", "VM", "--at", "200,320")
    sim.cli("comp", "add", "Oscope", "--label", "O", "--at", "520,320")
    sim.cli("wire", "add", "VM.lPin", "R2.rPin")
    vm_lead = [w for w in wires(sim).values() if "VM." in w.get("from", "") + w.get("to", "")]
    sim.cli("wire", "add", "O.Pin0", "@" + middle(vm_lead[0]))
    now = wires(sim)

    def ends(w):
        return [e.split(".")[0] for e in (w.get("from", ""), w.get("to", ""))]
    leads = sorted(u for u, w in now.items()
                   if "VM" in ends(w) or "O" in ends(w)
                   or (all(e.startswith("Node") for e in ends(w)) and not set(ends(w)) & old_nodes))
    net_now = sorted(u for u in now if u not in leads and u not in net_b)
    sim.check("leads: VM, scope and the stretch between the dots", int(len(leads) == 3), 1, absolute=0,
              note=str({u: now[u] for u in leads}))
    s = sim.cli("ui", "click", middle(now[net_now[0]]), "--cmd", "--double")
    sim.check("cmd+double-click: the net without the leads", int(marked(s) == net_now), 1, absolute=0,
              note="leads %s marked %s net %s" % (leads, marked(s), net_now))
    s = sim.cli("ui", "type", "*", "*")
    sim.check("*: the net without the leads", int(marked(s) == net_now), 1, absolute=0,
              note="leads %s marked %s" % (leads, marked(s)))
    s = sim.cli("ui", "click", middle(now[leads[0]]), "--cmd")
    sim.check("cmd+click a lead: it can be marked by hand", int(leads[0] in (marked(s) or [])), 1, absolute=0,
              note=str(marked(s)))
    sim.cli("ui", "type", "Escape")
    sim.cli("comp", "remove", "VM", "O")
    ws = wires(sim)
    net_a = sorted(u for u in ws if u not in net_b)
    sim.check("instruments removed: net A has 3 wires again", int(len(net_a) == 3), 1, absolute=0, note=str(ws))

    # Defaults: a band without alpha gets 50 %, width 6 px
    r = sim.cli("wire", "style", net_b[0], "--band", "#7860a0")
    sim.check("default opacity 50 %", int(r[0]["bandColor"] == "#807860a0"), 1, absolute=0, note=str(r))
    sim.cli("undo")

    # Colours: one undo step for several wires
    r = sim.cli("wire", "style", *net_a, "--color", "#4fc08d", "--band", "#7860a0", "--opacity", "35",
                "--width", "14")
    ok = all(w["color"] == "#4fc08d" and w["bandColor"] == "#597860a0" and w["bandWidth"] == 14 for w in r)
    sim.check("wire style: set on the 3 wires", int(ok), 1, absolute=0, note=str(r))
    sim.check("wire style: net B untouched", int("color" not in wires(sim)[net_b[0]]), 1, absolute=0)
    sim.cli("undo")
    sim.check("undo: all 3 back in one step", int(all("color" not in w and "bandColor" not in w
                                                      for w in wires(sim).values())), 1, absolute=0)
    sim.cli("redo")
    sim.check("redo: back", int(all(wires(sim)[u].get("color") == "#4fc08d" for u in net_a)), 1, absolute=0)
    r = sim.cli("wire", "style", net_a[0], "--width", "default")
    sim.check("--width default: no width saved", int("bandWidth" not in r[0]), 1, absolute=0, note=str(r))
    r = sim.cli("wire", "style", net_a[0])
    sim.check("wire style without options: prints", int(r[0].get("color") == "#4fc08d"), 1, absolute=0)

    # Save / open
    tmp = tempfile.mkdtemp(prefix="wirecolors-")
    path = os.path.join(tmp, "colors.sim1")
    sim.cli("circuit", "save", path)
    text = open(path).read()
    sim.check("saved: color and bandColor in the file", int('color="#4fc08d"' in text and 'bandColor="#597860a0"' in text),
              1, absolute=0)
    sim.check("saved: no style attributes on net B", int(text.count('color="') == 3), 1, absolute=0,
              note=str(text.count('color="')))
    sim.cli("circuit", "open", path, "--discard")
    after = wires(sim)
    sim.check("reopened: colours back", int(sum(1 for w in after.values() if w.get("color") == "#4fc08d") == 3
                                            and sum(1 for w in after.values() if w.get("bandWidth") == 14) == 2),
              1, absolute=0, note=str(after))

    # A split wire: both halves keep the colours
    styled = [u for u, w in after.items() if w.get("color")]
    sim.cli("comp", "add", "Resistor", "--label", "R5", "--at", "200,40")
    target = after[styled[0]]
    before = set(after)
    sim.cli("wire", "add", "R5.lPin", "@" + middle(target))
    now = wires(sim)
    new = [now[u] for u in set(now) - before]
    halves = [w for w in new if "R5" not in w.get("from", "") + w.get("to", "")]
    sim.check("split: both halves keep the colour", int(len(halves) == 2 and all(w.get("color") == "#4fc08d"
                                                                                 for w in halves)),
              1, absolute=0, note=str(new))
    sim.check("split: the new wire to R5 has none",
              int(all("color" not in w for w in new if "R5" in w.get("from", "") + w.get("to", ""))), 1, absolute=0)

    # Export: the bands are in the drawn area
    png = os.path.join(tmp, "colors.png")
    sim.cli("capture", "circuit", "--file", png)
    sim.check("capture: written", int(os.path.getsize(png) > 1000), 1, absolute=0)

    # Backspace deletes the marked wires in one step
    build(sim)
    ws = wires(sim)
    sim.cli("select", "R1")
    sim.cli("ui", "type", "=", "*")
    s = sim.cli("ui", "type", "Backspace")
    left = wires(sim)
    sim.check("backspace: the marked net deleted", int(len(left) == 1), 1, absolute=0, note=str(left.keys()))
    sim.check("backspace: back to parts mode", int(net(s) is None), 1, absolute=0)
    sim.cli("undo")
    sim.check("undo: all wires back", int(len(wires(sim)) == len(ws)), 1, absolute=0, note=str(wires(sim).keys()))


if __name__ == "__main__":
    run_test(test)
