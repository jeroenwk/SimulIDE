"""BH-012: a circuit with a Z80 must survive save + reopen.

The Z80 had a property id "Single cycle I/O" (spaces, slash): saved as an XML attribute
name it makes the .sim1 invalid, so reopening fails. Expected: the file opens and the
Z80 is still there with its properties (Producer, CMOS)."""

from harness import run_test


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Z80", "--label", "X1", "--at", "0,0")
    sim.cli("comp", "set", "X1", "CMOS=true")
    sim.cli("circuit", "save", "/tmp/bh012.sim1")
    ok = 1.0
    try:
        sim.cli("circuit", "open", "/tmp/bh012.sim1", "--discard")
    except Exception as e:
        print("  open failed:", str(e)[:200])
        ok = 0.0
    sim.check("circuit with Z80 reopens (1 = yes)", ok, 1.0, tol=0.01)
    if ok:
        g = sim.cli("comp", "get", "X1")
        cmos = [p["value"] for p in g["properties"] if p["name"] == "CMOS"][0]
        sim.check("CMOS kept (1 = true)", 1.0 if cmos == "true" else 0.0, 1.0, tol=0.01)


if __name__ == "__main__":
    run_test(test)
