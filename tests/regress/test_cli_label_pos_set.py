"""`comp set X valLabPos=x,y` / `idLabPos=x,y` stored the new label position but did not move
the label: it moved only after the circuit was saved and opened again.

Found 2026-11-10 while building the create-circuit skill (a regulator's value label sat on its
output wire, and setting valLabPos through the CLI did nothing visible).

Right result: right after `comp set`, the label's scene rectangle (`comp get X` -> labels[].rect)
starts at the part position plus the given offset (a label at rotation 0 is drawn from its
top-left corner), i.e. it moved by the same amount as the offset changed.
"""

from harness import run_test


def label_rect(sim, comp, kind):
    r = sim.cli("comp", "get", comp)
    for lab in r.get("labels", []):
        if lab["kind"] == kind:
            return lab["rect"]
    raise AssertionError(f"{comp} has no visible {kind} label: {r.get('labels')}")


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "160,96", "Resistance=470", "Show_id=true")
    for kind, prop in (("value", "valLabPos"), ("name", "idLabPos")):
        before = label_rect(sim, "R1", kind)
        sim.cli("comp", "set", "R1", f"{prop}=-40,-40")
        after = label_rect(sim, "R1", kind)
        print(f"[test] {kind} label rect before {before} after {prop}=-40,-40: {after}")
        sim.check(f"{kind} label x moved to part x - 40", after[0], 160 - 40, absolute=2)
        sim.check(f"{kind} label y moved to part y - 40", after[1], 96 - 40, absolute=2)


if __name__ == "__main__":
    run_test(test)
