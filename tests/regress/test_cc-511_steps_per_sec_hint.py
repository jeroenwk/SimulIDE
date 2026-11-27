"""`settings set stepsPerSec=...` says what to set instead (create-circuit #511).

It answered "stepsPerSec is read only", and a build that wanted a smaller solver step gave up.
stepsPerSec follows from the speed and stepSize: the refusal names stepSize (the solver step)
and reactStep (the reactive parts' step). Expected: the error mentions both.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    try:
        c("settings", "set", "stepsPerSec=1000")
        msg = ""
    except Exception as e:
        msg = str(e)
    sim.check("refusal names stepSize and reactStep: %r" % msg[-160:],
              int("stepSize" in msg and "reactStep" in msg), 1, absolute=0)


run_test(test)
