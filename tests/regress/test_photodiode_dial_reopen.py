"""A part's dial is where it was when the circuit is reopened.

The Photodiode placed its Lux dial at y 7 (Dialed's default) when added, and at y 12
(PhotoSensor::updateProxy) once a property was loaded: a layout checked clean when built
had the dial 5 px lower after save + reopen, over a wire (create-circuit, Scherz 5.21a).
Expected: for each part and rotation, `controls` after reopen == after `comp add`.
"""

import os, tempfile
from harness import run_test

PARTS = ["Photodiode", "Phototransistor", "Potentiometer"]


def controls(sim):
    return {c["label"]: c.get("controls") for c in sim.cli("comp", "list")}


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    for i, p in enumerate(PARTS):
        for j, rot in enumerate((0, 90, 180, 270)):
            c("comp", "add", p, "--label", "%s%d" % (p, rot), "--at", "%d,%d" % (j * 160, i * 160), "--rot", str(rot))
    before = controls(sim)
    f = os.path.join(tempfile.mkdtemp(), "dials.sim1")
    c("circuit", "save", f)
    c("circuit", "open", f, "--discard")
    after = controls(sim)
    for lab in sorted(before):
        sim.check("%s dial %s after reopen (was %s)" % (lab, after.get(lab), before[lab]),
                  int(after.get(lab) == before[lab]), 1, absolute=0)


run_test(test)
