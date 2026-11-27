"""A pin whose name holds '=' (the 74HC85's cascade input IA=B) can be named in the CLI
(create-circuit #548).

`U1.IA=B` was read as the setting `U1.IA` = `B`: `wire add U1.IA=B ...` failed with
"wire add takes no name=value settings", so the input could not be wired or probed.
A setting name never has a '.', so such a word is a pin reference.
Expected: the wire is made, the pin reads the 5 V it is tied to; `comp set R Resistance=2k`
still sets.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "74HC85", "--label", "U1", "--at", "0,0")
    c("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-120,0", "Voltage=5", "Out=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "-120,80")
    try:
        c("wire", "add", "V.outnod", "U1.IA=B")
        ok = 1
    except Exception as e:
        sim.check("wire add V.outnod U1.IA=B (%s)" % str(e)[-90:], 0, 1, absolute=0)
        ok = 0
    if ok:
        c("sim", "run", "--for", "1ms")
        v = c("probe", "U1.IA=B")[0]["voltage"]
        sim.check("U1.IA=B voltage", v, 5.0, tol=0.01)
        c("sim", "stop")
    c("comp", "set", "R", "Resistance=2k")
    props = {p["name"]: p["value"] for p in c("comp", "get", "R")["properties"]}
    sim.check("comp set still sets (Resistance %s)" % props.get("Resistance"),
              int(str(props.get("Resistance", "")).startswith("2")), 1, absolute=0)


run_test(test)
