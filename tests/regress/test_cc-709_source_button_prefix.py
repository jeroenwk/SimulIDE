"""A Current or Voltage Source's button shows its value with a unit prefix
(create-circuit #709).

It printed two decimals in base units: 1 uA read "0.00 A". Expected: "1.00 uA" for 1 uA,
"5.00 V" for 5 V, "2.50 mA" for 2.5 mA (four characters: the button holds no more).
"""

from harness import run_test


def text(c, lab):
    return next(x["text"] for x in c("ui", "controls", lab)["controls"] if x.get("kind") == "toggle")


def test(sim):
    c = sim.cli
    for typ, setting, want in (("Current Source", "Value_Amp=1uA", ("1.00", "uA")),
                               ("Current Source", "Value_Amp=2.5mA", ("2.50", "mA")),
                               ("Voltage Source", "Value_Volt=5", ("5.00", " V"))):
        c("circuit", "new", "--discard")
        c("comp", "add", typ, "--label", "S", "--at", "0,0", setting, "Running=true")
        c("comp", "add", "Resistor", "--label", "R", "--at", "80,0")
        c("sim", "run", "--for", "1ms")
        t = " ".join(text(c, "S").split())
        sim.check("%s %s shows %r" % (typ, setting, t), int(t.startswith(want[0]) and want[1].strip() in t), 1, absolute=0)
        c("sim", "stop")


run_test(test)
