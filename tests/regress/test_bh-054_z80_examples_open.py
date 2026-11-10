"""BH-054: four shipped Z80 examples cannot be opened at all.

The files were saved before BH-012 renamed the Z80 property id "Single cycle I/O"
(an invalid XML attribute name: spaces and a slash) to "Single_cycle_IO", and were
never regenerated, so they still carry the invalid attribute. QDomDocument rejects
the whole file: "not a valid circuit file, line 3: unexpected character", in the GUI
as well as through the CLI.

This test opens every example under resources/examples/Micro/Z80 and runs each for
20 ms of simulated time. All of them must load and run.
"""

import glob
import os

from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))
Z80_DIR = os.path.join(HERE, "..", "..", "resources", "examples", "Micro", "Z80")


def test(sim):
    files = sorted(glob.glob(os.path.join(Z80_DIR, "**", "*.sim1"), recursive=True))
    assert files, "no Z80 examples found in %s" % Z80_DIR

    opened = 0
    for f in files:
        rel = os.path.relpath(f, Z80_DIR)
        sim.cli("circuit", "open", f, "--discard")          # raises on a load error
        sim.cli("sim", "run", "--for", "20 ms")
        sim.cli("sim", "stop")
        opened += 1
        sim.log("opened %s" % rel)
    sim.check("Z80 examples that open and run 20 ms (of %d files)" % len(files),
              opened, len(files), absolute=0.5)


if __name__ == "__main__":
    run_test(test)
