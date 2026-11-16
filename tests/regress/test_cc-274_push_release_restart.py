"""A Push pressed and released through the CLI comes up released after sim stop (create-circuit #274).

`ui release` emitted `released()` (the Push unchecks itself there) and then toggled the
checkable button, as for a toggle switch: the Push stayed checked, and PushBase::stamp
took it for a button held at power-on. After the next `sim stop`, a normally-closed Push
came up open, a normally-open one closed: a capacitor charged through it "kept" its
charge (#319) and an SCR gated through it was on at t=0 (#275). Qt's own click toggles
before `released()`; the CLI now does the same.
"""

from harness import run_test


def build(sim, nc):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-80,0", "Voltage=5", "Out=true")
    c("comp", "add", "Push", "--label", "P", "--at", "0,0", "Norm_Close=" + ("true" if nc else "false"))
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,0")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,80")
    c("wire", "add", "V.outnod", "P.pinP0")
    c("wire", "add", "P.switch0pinN", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")


def after_restart(sim):
    c = sim.cli
    c("sim", "start")
    c("ui", "press", "P")
    c("sim", "run", "--for", "1ms")
    c("ui", "release", "P")
    c("sim", "run", "--for", "1ms")
    c("sim", "stop")
    c("sim", "run", "--for", "1ms")
    v = c("probe", "R.lPin")[0]["voltage"]
    c("sim", "stop")
    return v


def test(sim):
    build(sim, nc=True)
    sim.check("NC push released, after a restart: closed (V)", after_restart(sim), 5, absolute=0.05)
    build(sim, nc=False)
    sim.check("NO push released, after a restart: open (V)", after_restart(sim), 0, absolute=0.05)


run_test(test)
