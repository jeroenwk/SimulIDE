"""comp list / comp get report a part's flips (create-circuit #671).

The text direction of a vertical part's labels depends on its flips (rot 270 + flip h reads
like rot 90), but the CLI gave only `rotation`, so create-circuit's W-TEXT flagged a rot 270
SCR that was already flipped to read well, and its hint made it worse.
Expected: a resistor at rot 90 flipped h has hflip true and no vflip; flipped v too: both.
"""
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,0", "--rot", "90")
    r = c("comp", "get", "R")
    sim.check("plain: no hflip", int("hflip" in r), 0, absolute=0)
    c("comp", "flip", "R", "h")
    r = [x for x in c("comp", "list") if x["label"] == "R"][0]
    sim.check("flip h: hflip", int(r.get("hflip") is True), 1, absolute=0)
    sim.check("flip h: no vflip", int("vflip" in r), 0, absolute=0)
    c("comp", "flip", "R", "v")
    r = c("comp", "get", "R")
    sim.check("flip h + v: both", int(r.get("hflip") is True and r.get("vflip") is True), 1, absolute=0)


run_test(test)
