"""Two CLI quirks (TODO comp-set-resumes, probe-remove-hidden).

1. comp set on a paused simulation resumed it. Setters such as eResistor::setResSafe wrap the
   change in Simulator::pauseSim() / resumeSim(); pauseSim() did nothing while already paused,
   resumeSim() then resumed. A script changing a part while paused lost its timing. Now the pair
   nests: the run stays paused, and only sim resume (or the pause button) resumes.
2. comp remove did nothing on a Probe while instruments are hidden (settings instruments=false):
   removal goes by the selection, and Qt does not select a hidden item. tests/cli/smoke.sh failed
   twice on it with the user's settings.
"""

import time

from harness import run_test


def status(sim):
    s = sim.cli("sim", "status")
    return s["state"], int(s["time_ps"])


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=5")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    sim.cli("comp", "add", "Led", "--label", "D1", "--at", "160,-40")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("wire", "add", "B1.lPin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "D1.lPin")
    sim.cli("wire", "add", "D1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")

    # 1. Paused: comp set keeps it paused, the time stands still
    sim.cli("sim", "start")
    sim.cli("sim", "run", "--for", "1ms")
    state, t0 = status(sim)
    sim.check("paused after run --for (1 = paused)", 1 if state == "paused" else 0, 1, absolute=0.5,
              note=state)
    for comp, prop in (("R1", "Resistance=2000"), ("D1", "Threshold=2.0"), ("B1", "Voltage=6")):
        sim.cli("comp", "set", comp, prop)
        time.sleep(0.3)
        state, t = status(sim)
        sim.check("comp set %s %s: still paused (1)" % (comp, prop), 1 if state == "paused" else 0, 1,
                  absolute=0.5, note=state)
        sim.check("comp set %s: time unchanged (ps)" % comp, t, t0, absolute=0.5)
    res = [p["value"] for p in sim.cli("comp", "get", "R1")["properties"] if p["name"] == "Resistance"][0]
    sim.check("R1 took the new value (Ω)", float(res.split()[0]) * (1000 if "k" in res else 1), 2000, tol=0.001,
              note=res)
    # sim resume still resumes, sim pause still pauses
    sim.cli("sim", "resume")
    time.sleep(0.3)
    state, t = status(sim)
    sim.check("sim resume: running (1)", 1 if state == "running" and t > t0 else 0, 1, absolute=0.5,
              note="%s %d ps" % (state, t))
    sim.cli("comp", "set", "R1", "Resistance=1500")      # While running: stays running
    time.sleep(0.2)
    sim.check("comp set while running: still running (1)", 1 if status(sim)[0] == "running" else 0, 1,
              absolute=0.5)
    sim.cli("sim", "pause")
    sim.check("sim pause: paused (1)", 1 if status(sim)[0] == "paused" else 0, 1, absolute=0.5)
    sim.cli("sim", "stop")

    # 2. Instruments hidden: comp remove removes a Probe, its lead and the junction (wires as before)
    was = sim.cli("settings", "get", "instruments")
    sim.cli("settings", "set", "instruments", "false")
    try:
        sim.cli("comp", "add", "Probe", "--at", "160,-120", "--label", "PRB")
        w0 = sim.cli("circuit", "info")["wires"]
        sim.cli("wire", "add", "PRB.inPin", "R1.rPin")
        n0 = len(sim.cli("comp", "list"))
        sim.cli("comp", "remove", "PRB")
        labels = [c["label"] for c in sim.cli("comp", "list")]
        sim.check("hidden Probe removed (1)", 0 if "PRB" in labels else 1, 1, absolute=0.5,
                  note="%d → %d parts" % (n0, len(labels)))
        sim.check("one part less", len(labels), n0 - 1, absolute=0.5)
        sim.check("its lead removed too (wires)", sim.cli("circuit", "info")["wires"], w0, absolute=0.5)
    finally:
        sim.cli("settings", "set", "instruments", str(was).lower())


if __name__ == "__main__":
    run_test(test)
