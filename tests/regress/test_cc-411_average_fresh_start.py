"""`sim average` on a fresh run keeps the unsolved start out of min and max (create-circuit #411).

The averager read its channels once when the run started. On a fresh run (time 0) nothing
is solved yet: a 9 V battery's pins both read 0 V, so its current read 9 V / 1 mOhm =
-9000 A, and that one reading became the window's min (a build reported min -9000 A
against a steady -8.9 mA). The mean was right (the reading lasts 0 s).

Right result: 9 V across 1 kOhm, a fresh 1 ms average: current min = max = mean = 9 mA
(sign: into pin 0), voltage min = 9 V (not 0). A resumed run starts from a solved step
and keeps its start value (a static circuit has no later step: min = max = the value).
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B", "--at", "0,0", "Voltage=9")
    c("comp", "add", "Resistor", "--label", "R", "--at", "100,0", "Resistance=1k")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,100")
    c("wire", "add", "B.lPin", "R.lPin")
    c("wire", "add", "R.rPin", "G.Gnd")
    c("wire", "add", "B.rPin", "G.Gnd")
    c("sim", "stop")
    r = c("sim", "average", "--for", "1ms", "B")["channels"][0]
    sim.log("fresh: %s" % r)
    sim.check("fresh: current min (A)", abs(r["current"]["min"]), 0.009, tol=0.001)
    sim.check("fresh: current max (A)", abs(r["current"]["max"]), 0.009, tol=0.001)
    sim.check("fresh: voltage min (V)", r["voltage"]["min"], 9.0, tol=0.001)
    r = c("sim", "average", "--for", "1ms", "B")["channels"][0]
    sim.log("resumed: %s" % r)
    sim.check("resumed: current min (A)", abs(r["current"]["min"]), 0.009, tol=0.001)
    sim.check("resumed: voltage max (V)", r["voltage"]["max"], 9.0, tol=0.001)


if __name__ == "__main__":
    run_test(test)
