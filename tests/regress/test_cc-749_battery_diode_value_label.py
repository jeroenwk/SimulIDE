"""A new Battery shows its voltage and a new Diode its model as the value label, like a
Resistor its resistance (create-circuit #749).

Neither had a shown property, so Show_Val=true drew nothing and a drawing read without the
battery voltage or the diode type. Expected: Battery "5 V", then "12 V" after Voltage=12;
Diode "Diode Default", then "1N4007" after Model=1N4007. A circuit saved with Show_Val
false (every existing file) reopens without the label.
"""

import os, tempfile

from harness import run_test


def val_label(c, lab):
    return next((x["text"] for x in c("comp", "get", lab).get("labels") or [] if x["kind"] == "value"), None)


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B", "--at", "0,0")
    c("comp", "add", "Diode", "--label", "D", "--at", "80,0")
    for lab, want in (("B", "5 V"), ("D", "Diode Default")):
        t = val_label(c, lab)
        sim.check("%s value label %r" % (lab, t), int(t == want), 1, absolute=0)
    c("comp", "set", "B", "Voltage=12")
    c("comp", "set", "D", "Model=1N4007")
    for lab, want in (("B", "12 V"), ("D", "1N4007")):
        t = val_label(c, lab)
        sim.check("%s value label after the change %r" % (lab, t), int(t == want), 1, absolute=0)
    c("comp", "set", "B", "Show_Val=false")
    c("comp", "set", "D", "Show_Val=false")
    f = os.path.join(tempfile.mkdtemp(), "labels.sim1")
    c("circuit", "save", f)
    c("circuit", "open", f, "--discard")
    for lab in ("B", "D"):
        t = val_label(c, lab)
        sim.check("%s saved with Show_Val false: no label after reopen (%r)" % (lab, t), int(t is None), 1, absolute=0)


run_test(test)
