"""`ui press` + `ui release` on a toggle switch (create-circuit #170).

In the GUI a mouse click on a Switch toggles it (on release). Through the CLI,
`ui press SW` then `ui release SW` only set and cleared the button's down flag:
the switch never toggled, so a build drove its switches with `comp set Toggled=` instead.
A press + release must toggle a toggle switch once, like `ui click`; a Push (momentary)
still closes while held and opens on release.
"""

from harness import run_test


def toggled(sim, lab):
    return {q["name"]: q["value"] for q in sim.cli("comp", "get", lab, "--all")["properties"]}.get("Toggled")


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Switch", "--label", "S", "--at", "0,0")
    c("ui", "press", "S")
    c("ui", "release", "S")
    sim.check("Switch toggled by press + release (%s)" % toggled(sim, "S"), int(toggled(sim, "S") == "true"), 1,
              absolute=0)
    c("ui", "press", "S")
    c("ui", "release", "S")
    sim.check("and back by a second one (%s)" % toggled(sim, "S"), int(toggled(sim, "S") == "false"), 1, absolute=0)
    c("ui", "click", "S")
    sim.check("ui click still toggles (%s)" % toggled(sim, "S"), int(toggled(sim, "S") == "true"), 1, absolute=0)

    c("circuit", "new", "--discard")                  # a momentary push: closed only while held
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-80,0", "Voltage=5", "Out=true")
    c("comp", "add", "Push", "--label", "P", "--at", "0,0")
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,0")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,80")
    c("wire", "add", "V.outnod", "P.pinP0")
    c("wire", "add", "P.switch0pinN", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")
    c("sim", "start")
    c("ui", "press", "P")
    c("sim", "run", "--for", "1ms")
    held = c("probe", "R.lPin")[0]["voltage"]
    c("ui", "release", "P")
    c("sim", "run", "--for", "1ms")
    rel = c("probe", "R.lPin")[0]["voltage"]
    c("sim", "stop")
    sim.check("Push held: closed (V)", held, 5, absolute=0.05)
    sim.check("Push released: open (V)", rel, 0, absolute=0.05)


run_test(test)
