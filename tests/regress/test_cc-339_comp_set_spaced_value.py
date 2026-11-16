"""`comp set` with a value the shell split at its space (create-circuit #339).

`comp set CLK Duty=25 %` reaches SimulIDE as `CLK`, `Duty=25`, `%`: the leftover `%` was
reported as a second component ("one comp set per component"), which sent the builder
looking for the wrong mistake. A leftover word that is no component must say to quote
the value; a second component keeps the old message (#181).
"""

from harness import run_test


def err_of(c, *args):
    try:
        c(*args)
    except Exception as e:
        return str(e)
    return ""


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "0,80")
    err = err_of(c, "comp", "set", "R1", "Resistance=2", "k")
    sim.check("a split-off unit asks for quotes (%s)" % err[-110:], int("quotes" in err), 1, absolute=0)
    err = err_of(c, "comp", "set", "R1", "Resistance=2000", "R2", "Resistance=3000")
    sim.check("a second part still says one component (%s)" % err[-90:], int("one component" in err), 1, absolute=0)
    c("comp", "set", "R1", "Resistance=2 k")                      # quoted: one argument
    props = {q["name"]: q["value"] for q in c("comp", "get", "R1")["properties"]}
    sim.check("the quoted value is stored (%s)" % props.get("Resistance"),
              int(str(props.get("Resistance")).startswith("2")), 1, absolute=0)


run_test(test)
