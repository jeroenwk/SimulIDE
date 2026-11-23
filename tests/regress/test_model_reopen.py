"""A part saved with a named model must reopen with that model, not Custom.

Found by a book build (Scherz Fig. 5.33): an OptoTriac saved as Model="MOC3041" came back as
Custom. Its parameters are saved as text ("400 µA") and read back as 400*1e-6, which is one ulp
off the table's 400e-6; the setter compared exactly and switched the part to Custom.
Each named model of the parts with a model table: save, reopen, read the Model property.
"""

import os
import tempfile
from harness import run_test

CASES = [("OptoTriac", ["MOC3021", "MOC3041"]), ("Optocoupler", ["4N35"]), ("OptoLogic", ["6N137"]),
         ("Photodiode", ["BPW34"]), ("Phototransistor", ["BPW85A", "BPW85B", "BPW85C"]),
         ("Crystal", ["32.768 kHz", "4 MHz", "10 MHz"])]


def test(sim):
    c = sim.cli
    path = os.path.join(tempfile.mkdtemp(prefix="model-reopen-"), "m.sim1")
    for part, models in CASES:
        for m in models:
            c("circuit", "new", "--discard")
            c("comp", "add", part, "--label", "U", "--at", "0,0", "Model=%s" % m)
            c("circuit", "save", path)
            c("circuit", "open", path, "--discard")
            got = [q["value"] for q in c("comp", "get", "U")["properties"] if q["name"] == "Model"][0]
            sim.check("%s %s reopens as %s" % (part, m, got), int(str(got) == m), 1, absolute=0)


run_test(test)
