"""comp set / comp add say when the part stores another value than asked (create-circuit #293).

Battery Voltage=-9 stored 1e-12 V (the battery clamps at zero: its polarity is the pins)
and the reply showed only the stored value, so a build measured a "reversed battery" that
was not there. The reply now carries a note naming the asked and the stored value; a
value stored as asked gets no note.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    r = c("comp", "add", "Battery", "--label", "BAT", "--at", "0,0", "Voltage=-9")
    sim.check("comp add: note on a clamped value", int("stored" in r.get("note", "")), 1, absolute=0)
    r = c("comp", "set", "BAT", "Voltage=-9")
    sim.check("comp set: note on a clamped value", int("stored" in r.get("note", "")), 1, absolute=0)
    r = c("comp", "set", "BAT", "Voltage=12", "Resistance=50m")
    sim.check("comp set: no note when stored as asked", int("note" in r), 0, absolute=0)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,80")
    r = c("comp", "set", "R1", "Resistance=4.7k")
    sim.check("prefixed value: no note", int("note" in r), 0, absolute=0)


run_test(test)
