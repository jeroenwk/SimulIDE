"""BH-050: `settings set stepSize` silently changes the step you ask for.

At 100 % speed a 1 ns step means 1e9 steps per second, the limit the step
logic keeps under (as the Settings dialog), so `settings set stepSize 1ns`
answered "1 us": the step was raised by 1000 without a word. The other way
round, `settings set speed 100` with a 1 ns step raised the step too.

Right result: an explicit step is kept and the speed is lowered to fit, and
the reply says so; a speed change that has to move the step says so.
"""

from harness import run_test


def row(reply, name):
    rows = reply if isinstance(reply, list) else [reply]
    return [r for r in rows if r.get("name") == name][0]


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("settings", "set", "speed", "100")
    sim.cli("settings", "set", "stepSize", "1 us")

    r = row(sim.cli("settings", "set", "stepSize", "1 ns"), "stepSize")
    sim.log("stepSize 1 ns at 100 %%: %s" % r)
    sim.check("stepSize kept at 1 ns", int(r["value"] == "1 ns"), 1, absolute=0)
    sim.check("reply says the speed was lowered", int("speed" in r.get("note", "")), 1, absolute=0)
    speed = float(sim.cli("settings", "get", "speed"))
    sim.check("speed lowered below 100 %", int(speed < 100), 1, absolute=0)

    r = row(sim.cli("settings", "set", "speed", "100"), "speed")
    sim.log("speed 100 %% with a 1 ns step: %s" % r)
    step = sim.cli("settings", "get", "stepSize")
    sim.log("step now %s" % step)
    sim.check("speed change that moves the step says so", int(step == "1 ns" or "step" in r.get("note", "")), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
