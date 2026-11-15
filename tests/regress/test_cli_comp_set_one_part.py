"""`comp set` with several parts in one command (create-circuit #181).

`comp set A1 Toggled=true A2 Toggled=false` set only A1, dropped A2 without a word and
applied the last value of each key (A1 Toggled=false): two truth-table rows of a build
were measured in the wrong switch state. A second component name must be an error.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "0,80")
    err = ""
    try:
        c("comp", "set", "R1", "Resistance=2000", "R2", "Resistance=3000")
    except Exception as e:
        err = str(e)
    sim.check("two parts in one comp set is an error (%s)" % err[-90:], int("one component" in err), 1, absolute=0)
    props = {q["name"]: q["value"] for q in c("comp", "get", "R1")["properties"]}
    sim.check("R1 unchanged after the refused command (%s)" % props.get("Resistance"),
              int(str(props.get("Resistance")).startswith("100")), 1, absolute=0)
    c("comp", "set", "R2", "Resistance=3000")                     # one part: still works
    props = {q["name"]: q["value"] for q in c("comp", "get", "R2")["properties"]}
    sim.check("one part per comp set works (%s)" % props.get("Resistance"),
              int(str(props.get("Resistance")).startswith("3")), 1, absolute=0)


run_test(test)
