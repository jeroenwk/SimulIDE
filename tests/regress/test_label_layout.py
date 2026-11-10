"""Label layout (specs/label-layout.md): a part's name and value on one line, on the value's side.

- A new resistor with its name shown has the name and the value at the same height (same
  font, so the same baseline), below the part (Comp2Pin keeps its value below), name first,
  starting left of the part's centre (the line is centred on it).
- Saved and reopened, it is still laid out automatically (labelAuto="true").
- A position set by hand (comp set idLabPos) switches the part to manual.
- A circuit saved before this change (no labelAuto attribute) keeps its saved positions.
"""

import os
import re
import tempfile

from harness import run_test


def attrs(path, label):
    text = open(path, encoding="utf-8").read()
    for item in re.findall(r"<item [^>]*>", text):
        if 'label="%s"' % label in item:
            return dict(re.findall(r'(\w+)="([^"]*)"', item))
    return {}


def xy(s):
    return [float(v) for v in s.split(",")]


def test(sim):
    d = tempfile.mkdtemp(prefix="labels-")
    f1 = os.path.join(d, "new.sim1")

    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "0,0", "Show_id=true", "Show_Val=true")
    sim.cli("circuit", "save", f1)
    a = attrs(f1, "R1")
    sim.log("new R1: idLabPos %s valLabPos %s labelAuto %s" % (a.get("idLabPos"), a.get("valLabPos"), a.get("labelAuto")))
    idp, valp = xy(a["idLabPos"]), xy(a["valLabPos"])
    sim.check("new part: automatic", int(a.get("labelAuto") == "true"), 1, absolute=0)
    sim.check("name and value on one line (y)", idp[1] - valp[1], 0, absolute=0.01)
    sim.check("line below the part (y > 0)", int(idp[1] > 0), 1, absolute=0)
    sim.check("name before the value (x)", int(idp[0] < valp[0]), 1, absolute=0)
    sim.check("line starts left of the centre", int(idp[0] < 0), 1, absolute=0)

    sim.cli("circuit", "open", f1, "--discard")
    f2 = os.path.join(d, "reopened.sim1")
    sim.cli("circuit", "save", f2)
    b = attrs(f2, "R1")
    sim.check("reopened: still automatic", int(b.get("labelAuto") == "true"), 1, absolute=0)
    sim.check("reopened: same name position", int(b.get("idLabPos") == a.get("idLabPos")), 1, absolute=0)

    sim.cli("comp", "set", "R1", "idLabPos=0,-30")
    f3 = os.path.join(d, "manual.sim1")
    sim.cli("circuit", "save", f3)
    c = attrs(f3, "R1")
    sim.check("position set by hand: manual", int(c.get("labelAuto") == "false"), 1, absolute=0)
    sim.check("position set by hand: kept", int(c.get("idLabPos") == "0,-30"), 1, absolute=0)

    # An older file: the same circuit with the old default positions and no labelAuto
    old = open(f1, encoding="utf-8").read()
    old = re.sub(r' labelAuto="[^"]*"', "", old)
    old = re.sub(r'idLabPos="[^"]*"', 'idLabPos="-16,-24"', old)
    old = re.sub(r'valLabPos="[^"]*"', 'valLabPos="-16,6"', old)
    f4 = os.path.join(d, "old.sim1")
    open(f4, "w", encoding="utf-8").write(old)
    sim.cli("circuit", "open", f4, "--discard")
    f5 = os.path.join(d, "old_saved.sim1")
    sim.cli("circuit", "save", f5)
    e = attrs(f5, "R1")
    sim.log("old file R1: idLabPos %s valLabPos %s labelAuto %s" % (e.get("idLabPos"), e.get("valLabPos"), e.get("labelAuto")))
    sim.check("old file: manual", int(e.get("labelAuto") == "false"), 1, absolute=0)
    sim.check("old file: name kept", int(e.get("idLabPos") == "-16,-24"), 1, absolute=0)
    sim.check("old file: value kept", int(e.get("valLabPos") == "-16,6"), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
