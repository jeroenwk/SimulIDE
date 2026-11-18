"""K6 (specs/keyboard-input.md): Space opens the properties of the selected part.

Expected values follow the spec: with exactly one part selected, Space opens its properties
dialog and closing the dialog gives the canvas focus back (in headless tests the dialog is
closed by the server's filter, which leaves a "dialog closed (headless)" note on the reply);
with nothing or two parts selected Space does nothing. Nothing is simulated.
"""

import json
import os
import subprocess
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def raw(sim, *args):
    """One CLI call, keeping the whole reply (sim.cli returns only its result)."""
    cmd = [sim.bin, "cli"]
    if sim.socket:
        cmd += ["--socket", sim.socket]
    cmd += ["--json"] + [str(a) for a in args]
    out = subprocess.run(cmd, capture_output=True, text=True, timeout=120)
    return json.loads(out.stdout)


def opened(reply):
    return any("dialog closed (headless)" in m for m in reply.get("messages", []))


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    sim.cli("comp", "add", "Resistor", "--label", "R2", "--at", "300,100")

    # One part selected: Space opens its properties, focus returns to the canvas
    sim.cli("select", "R1")
    r = raw(sim, "ui", "type", "space")
    sim.check("space opens the properties dialog", int(opened(r)), 1, absolute=0)
    sim.check("canvas focused after the dialog closed", int(r["result"]["focused"]), 1, absolute=0)
    sim.check("part stays selected", len(r["result"]["selection"]), 1, absolute=0)

    # The dialog is reused for the same part; space opens it again
    r = raw(sim, "ui", "type", "space")
    sim.check("space opens it again", int(opened(r)), 1, absolute=0)

    # Nothing selected: no dialog
    sim.cli("select", "--none")
    r = raw(sim, "ui", "type", "space")
    sim.check("nothing selected: no dialog", int(opened(r)), 0, absolute=0)

    # Two parts selected: no dialog
    sim.cli("select", "R1", "R2")
    r = raw(sim, "ui", "type", "space")
    sim.check("two parts selected: no dialog", int(opened(r)), 0, absolute=0)

    # A junction dot is not a part: no dialog either. (R1.rPin needs a wire first;
    # the second wire add then inserts the dot and reports its pin as "from")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "500,100")
    sim.cli("wire", "add", "R1.rPin", "R2.lPin")
    w = sim.cli("wire", "add", "R1.rPin", "R3.lPin")
    node = w["from"].split(".")[0]
    sim.check("a junction was inserted", int(node.startswith("Node")), 1, absolute=0, note=node)
    sim.cli("select", "--none")
    sim.cli("select", node)
    r = raw(sim, "ui", "type", "space")
    sim.check("junction selected: no dialog", int(opened(r)), 0, absolute=0)

    # Registry
    sc = sim.cli("kbd", "shortcuts")
    sim.check("registry: Space opens properties", int(any(x["keys"] == "Space" for x in sc)), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
