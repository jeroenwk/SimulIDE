"""CLI part geometry gives the rectangles of a part's on-canvas controls ("controls").

A Dialed part (Photodiode, Phototransistor, Potentiometer, variable source) draws a dial
widget below its symbol, outside `body`, so a layout check that only knows `body` and
`labels` can't see a dial over a neighbour's wire or label (create-circuit, Scherz 5.21b
and 5.26b). Expected:
- Photodiode at 0,0, dial shown: `controls` holds one rectangle, below the symbol
  (its top at y >= 0) and centred on x = 0.
- Light_Pin on (the dial is hidden): no `controls`.
- Potentiometer at 0,0: one rectangle.
- Resistor: no `controls`.
"""

from harness import run_test


def geo(sim, label):
    for c in sim.cli("comp", "list"):
        if c["label"] == label:
            return c
    raise KeyError(label)


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0")
    c("comp", "add", "Potentiometer", "--label", "P", "--at", "200,0")
    c("comp", "add", "Resistor", "--label", "R", "--at", "400,0")
    d = geo(sim, "D")
    ctl = d.get("controls", [])
    sim.check("photodiode with dial: one control", len(ctl), 1, absolute=0)
    if ctl:
        x, y, w, h = ctl[0]
        sim.check("dial below the symbol (top %d >= 0)" % y, int(y >= 0), 1, absolute=0)
        sim.check("dial centred on x 0 (%d..%d)" % (x, x + w), int(abs(x + w / 2) <= 2 and w > 10), 1, absolute=0)
    sim.check("potentiometer: one control", len(geo(sim, "P").get("controls", [])), 1, absolute=0)
    sim.check("resistor: no control", len(geo(sim, "R").get("controls", [])), 0, absolute=0)
    c("comp", "set", "D", "Light_Pin=true")
    sim.check("light pin on, dial hidden: no control", len(geo(sim, "D").get("controls", [])), 0, absolute=0)


run_test(test)
