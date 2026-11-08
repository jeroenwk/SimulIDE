"""BH-010: every component offered by the component list can be created.

`lib list` returns what the Components panel offers. resources/data/icCD.xml lists CD4521
("24-stage frequency divider and oscillator") but resources/data/ICs/ has no CD4521 folder, so
adding it fails ("could not create CD4521"): a dead entry in the palette.
Expected: every listed name can be added, so the number of failures is 0.
"""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    items = sim.cli("lib", "list")
    failed = []
    for i, it in enumerate(items):
        try:
            sim.cli("comp", "add", it["name"], "--label", "Z%d" % i, "--at", "%d,%d" % ((i % 10) * 200, (i // 10) * 200))
        except Exception as e:
            failed.append(it["name"])
            print("cannot create %s: %s" % (it["name"], str(e)[:100]))
    print("%d listed, %d cannot be created: %s" % (len(items), len(failed), failed))
    sim.check("listed components that cannot be created", len(failed), 0, absolute=0.5)


if __name__ == "__main__":
    run_test(test)
