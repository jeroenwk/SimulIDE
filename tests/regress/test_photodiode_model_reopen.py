"""A Photodiode keeps its model when a circuit is reopened: "Solar cell" turned "Custom".

A parameter set away from the model's table value turns the model Custom (PhotoSensor::custom),
compared with a relative tolerance of 1e-9 (`same()`). A DoubProp is saved with 6 significant
digits (QString::number): the Solar cell's IS, 0.1/(exp(0.6/0.025865)-1) = 8.423830...e-12 A,
is saved as "8.42383e-12 A" and read back about 1e-6 off, so the reopened part showed Model
Custom (its numbers were still those of the solar cell). BPW34's table values are short and
were not hit.

Expected: every Photodiode and Phototransistor model reads back as itself after save + reopen.
"""

import os, tempfile

from harness import run_test


def model_options(c, lab):
    p = next(p for p in c("comp", "get", lab)["properties"] if p["name"] == "Model")
    return [o for o in p["options"] if o != "Custom"]


def test(sim):
    c = sim.cli
    for typ in ("Photodiode", "Phototransistor"):
        c("circuit", "new", "--discard")
        c("comp", "add", typ, "--label", "P", "--at", "0,0")
        for m in model_options(c, "P"):
            c("circuit", "new", "--discard")
            c("comp", "add", typ, "--label", "P", "--at", "0,0", "Model=" + m)
            f = os.path.join(tempfile.mkdtemp(), "pd.sim1")
            c("circuit", "save", f)
            c("circuit", "open", f, "--discard")
            got = next(p.get("value") for p in c("comp", "get", "P")["properties"] if p["name"] == "Model")
            sim.check("%s %r after reopen: %r" % (typ, m, got), int(got == m), 1, absolute=0)


run_test(test)
