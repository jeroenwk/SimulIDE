"""A diode loaded from a file shows its model as the value label, not the default model.

The value label (create-circuit #749) was refreshed only in Diode::setPropStr, but the loader
sets each property through ComProperty::setValStr, so a reopened diode kept the label of its
constructor: "Diode Default" with Model=1N4007 (seen on SimulIDE PR #104's AC demo).

Expected: a Diode with Model=1N4007 and a Zener with another model than its default show
that model after save + reopen; and still after a model change is undone (that path went
through setPropStr and was right: kept as a guard).
"""

import os, tempfile

from harness import run_test


def val_label(c, lab):
    return next((x["text"] for x in c("comp", "get", lab).get("labels") or [] if x["kind"] == "value"), None)


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Diode", "--label", "D", "--at", "0,0", "Model=1N4007")
    c("comp", "add", "Zener", "--label", "Z", "--at", "80,0")
    zm = "Zener Default 3.3V"   # resources/data/diodes.model
    c("comp", "set", "Z", "Model=" + zm)
    want = {"D": "1N4007", "Z": zm}
    for lab in want:
        t = val_label(c, lab)
        sim.check("%s value label before saving %r" % (lab, t), int(t == want[lab]), 1, absolute=0)

    f = os.path.join(tempfile.mkdtemp(), "diode_label.sim1")
    c("circuit", "save", f)
    c("circuit", "open", f, "--discard")
    for lab in want:
        t = val_label(c, lab)
        sim.check("%s value label after reopen %r (want %r)" % (lab, t, want[lab]),
                  int(t == want[lab]), 1, absolute=0)

    c("comp", "set", "D", "Model=1N5819 Schottky")
    c("undo")
    t = val_label(c, "D")
    sim.check("D value label after undo of a model change %r" % t, int(t == "1N4007"), 1, absolute=0)


run_test(test)
