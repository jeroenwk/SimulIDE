"""BH-013: `circuit open` must open every circuit in resources/examples.

Older circuit files (e.g. Active/Mosfet/mosfet-H_Bridge.sim1, 'Text="... &#x3D ON"')
write the characters < = > as `&#x3C` `&#x3D` `&#x3E` without the closing ";".
The GUI loader reads the file line by line and accepts them (StrBaseProp
"Older files"), but the CLI pre-check in cli_circuit.cpp parses the file with
QDomDocument and refuses it ("not a valid circuit file"). Right result: the
CLI opens what the GUI opens. Files that fail for another reason (the Z80
property name, BH-012) are not counted here.
"""
import glob, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "resources", "examples")

def test(sim):
    files = sorted(glob.glob(os.path.join(ROOT, "**", "*.sim1"), recursive=True))
    bad = []
    for f in files:
        try:
            sim.cli("circuit", "open", f, "--discard", timeout=60)
        except Exception as e:
            print("[bh-013] cannot open %s: %s" % (f, str(e)[:120]))
            if "Single cycle I/O" in open(f, errors="replace").read(): continue   # BH-012
            bad.append(os.path.relpath(f, ROOT))
    sim.check("examples that fail to open", len(bad), 0, tol=0, note=",".join(bad))

run_test(test)
