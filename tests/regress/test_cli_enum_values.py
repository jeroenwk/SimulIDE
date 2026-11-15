"""comp set / comp add with an enum setting (create-circuit issue #160).

`comp set W Wave_Type=triangle` was accepted and echoed back, but WaveGen only knows
"Triangle", so it kept making a sine; any string (even "bogus") was stored. Enum
values are now matched to the options without case (stored in their canonical
spelling), a whole-number index is still passed on (setters accept "2"), and
anything else is refused with the options in the message.
"""

from harness import run_test


def enum(sim, comp, name):
    d = sim.cli("comp", "get", comp, "--all")
    return next(p["value"] for p in d["properties"] if p["name"] == name)


def refused(sim, *args):
    try:
        sim.cli(*args)
    except Exception as e:          # the CLI's error reply
        return str(e)
    return ""


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "0,0")
    c("comp", "set", "W", "Wave_Type=triangle")
    sim.check("Wave_Type=triangle is stored as Triangle", 1 if enum(sim, "W", "Wave_Type") == "Triangle" else 0, 1, absolute=0,
              note=enum(sim, "W", "Wave_Type"))
    c("comp", "set", "W", "Wave_Type=Square")
    sim.check("exact value kept", 1 if enum(sim, "W", "Wave_Type") == "Square" else 0, 1, absolute=0)
    err = refused(sim, "comp", "set", "W", "Wave_Type=bogus")
    sim.check("unknown value refused", 1 if err else 0, 1, absolute=0, note=err[:120])
    sim.check("the refusal names the options", 1 if "Triangle" in err else 0, 1, absolute=0)
    sim.check("refused value not stored", 1 if enum(sim, "W", "Wave_Type") == "Square" else 0, 1, absolute=0)
    err = refused(sim, "comp", "add", "WaveGen", "--label", "W2", "--at", "0,96", "Wave_Type=bogus")
    sim.check("comp add refuses it too", 1 if err else 0, 1, absolute=0, note=err[:120])
    c("comp", "add", "WaveGen", "--label", "W3", "--at", "0,160", "Wave_Type=saw")
    sim.check("comp add matches without case", 1 if enum(sim, "W3", "Wave_Type") == "Saw" else 0, 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
