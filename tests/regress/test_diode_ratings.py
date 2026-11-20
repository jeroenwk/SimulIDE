"""Diode ratings as in a datasheet: Max Current is IF(AV) (an average), Surge Current is IFSM (a peak).

Before, the current at each GUI frame was compared with Max Current: the 1N4007 bridge of
led_driver board_mains (1.9 A peaks, 27 mA average) flashed as overloaded, although a 1 A
rectifier takes 30 A for an 8.3 ms half-sine. Now |I| goes through a 100 ms first-order
low-pass and is compared with Max Current (OVER, back below 0.95 x), and the current of each
solved step with Surge Current (SURGE, held 100 ms after the last peak).

Right results:
- 2 A DC (10 V, 4.5 Ohm): the average after 50 ms is 2 (1 - e^-0.5) of the current (low-pass,
  tau 100 ms); after 500 ms it is the current (within 1 %) and the state is OVER;
- a 50 Hz half-wave of 1.8 A peaks: the average is the mean |I| (within 10 %: the low-pass keeps a
  ripple), no OVER, no SURGE;
- 36 A through 0.2 Ohm for 250 ms (2 Hz square): SURGE, current_peak = |I|; still SURGE 50 ms
  after the current stops, OFF 150 ms after;
- a diode with Is 3 uA reverse-biased after a forward half: OFF (it leaks Is), not BREAKDOWN;
- Model 1N4007 and ES1J bring their ratings (1 A, 30 A); SurgeCurrent survives save and open.
"""
import math
import os
import tempfile
from harness import run_test

TAU = 0.1


def build(sim, src="Battery", volt=10, r=4.5):
    c = sim.cli
    c("circuit", "new", "--discard")
    if src == "Battery":
        c("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=%g" % volt)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "48,-48", "Resistance=%g" % r)
    c("comp", "add", "Diode", "--label", "D1", "--at", "96,0", "--rot", "-90", "Show_State=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,96")
    c("wire", "add", "B1.lPin", "R1.lPin"); c("wire", "add", "R1.rPin", "D1.lPin")   # B1.lPin is +
    c("wire", "add", "D1.rPin", "G.Gnd"); c("wire", "add", "B1.rPin", "G.Gnd")


def wave(sim, *props, r):
    """WaveGen (driving against ground) -> R1 -> D1 -> ground."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W1", "--at", "0,0", "Running=true", "Out=true", *props)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "48,-48", "Resistance=%g" % r)
    c("comp", "add", "Diode", "--label", "D1", "--at", "96,0", "--rot", "-90", "Show_State=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,96")
    c("wire", "add", "W1.outnod", "R1.lPin"); c("wire", "add", "R1.rPin", "D1.lPin")
    c("wire", "add", "D1.rPin", "G.Gnd")


def test(sim):
    c = sim.cli
    build(sim)                                          # 2 A DC
    sim.run("50ms")
    d = sim.read("D1")
    i = abs(d["current"])
    sim.check("DC: current (A)", i, 2.0, tol=0.05)
    sim.check("DC, 50 ms: average = I (1 - e^-0.5) (A)", d["current_avg"], i * (1 - math.exp(-0.05 / TAU)), tol=0.01)
    sim.check("DC, 50 ms: not OVER yet", int(d["state"] != "OVER"), 1, absolute=0, note=d["state"])
    sim.run("450ms")
    d = sim.read("D1")
    sim.check("DC, 500 ms: average = I (A)", d["current_avg"], i * (1 - math.exp(-0.5 / TAU)), tol=0.01)
    sim.check("DC, 500 ms: OVER (2 A average > 1 A)", int(d["state"] == "OVER"), 1, absolute=0, note=d["state"])

    c("sim", "stop")

    # 40 A for 250 ms, then nothing: a 2 Hz square wave (a timed source: a comp set while paused
    # resumes the run, so a change from the CLI does not land at a known time)
    wave(sim, "Wave_Type=Square", "Freq=2Hz", "Semi_Ampli=5", "Mid_Volt=5", r=0.2)
    c("comp", "set", "D1", "MaxCurrent=100")            # Only the surge rating in play
    sim.run("100ms")
    d = sim.read("D1")
    sim.check("40 A: SURGE (> 30 A)", int(d["state"] == "SURGE"), 1, absolute=0, note=d["state"])
    sim.check("40 A: current_peak = |I| (A)", d["current_peak"], abs(d["current"]), tol=1e-6)
    sim.check("40 A: |I| (A)", abs(d["current"]), 36, tol=0.1)
    sim.run("200ms")                                    # 300 ms: off for 50 ms
    d = sim.read("D1")
    sim.check("off for 50 ms: still SURGE (held 100 ms)", int(d["state"] == "SURGE"), 1, absolute=0,
              note="%s, I %.3g A" % (d["state"], d["current"]))
    sim.run("100ms")                                    # 400 ms: off for 150 ms
    d = sim.read("D1")
    sim.check("off for 150 ms: OFF", int(d["state"] == "OFF"), 1, absolute=0, note=d["state"])
    c("sim", "stop")

    wave(sim, "Semi_Ampli=10", "Freq=50Hz", r=5)          # 50 Hz half-wave, 1.8 A peaks
    sim.run("500ms")
    avg = c("sim", "average", "--for", "200ms", "D1")["channels"][0]["current"]
    d = sim.read("D1")
    sim.check("half-wave: peak current (A)", avg["max"], 1.8, tol=0.1)
    sim.check("half-wave: average = mean |I| (A)", d["current_avg"], avg["mean"], tol=0.10)
    sim.check("half-wave: no OVER, no SURGE", int(d["state"] in ("ON", "OFF")), 1, absolute=0, note=d["state"])
    c("sim", "stop")

    # A junction with a large saturation current (3 uA, the ES1J fitted to led_driver's D3A) leaks
    # Is when reverse-biased: after a forward half it is OFF, not BREAKDOWN (it read BREAKDOWN)
    wave(sim, "Wave_Type=Square", "Freq=50Hz", "Semi_Ampli=10", r=100)
    c("comp", "set", "D1", "SatCurrent=3uA")
    sim.run("15ms")                                     # Reverse half since 10 ms
    d = sim.read("D1")
    sim.check("Is 3 uA, reverse-biased after a forward half: OFF", int(d["state"] == "OFF"), 1, absolute=0,
              note="%s, %.3g V, %.3g A" % (d["state"], d["voltage"], d["current"]))
    c("sim", "stop")

    def ratings():
        p = {q["name"]: q["value"] for q in c("comp", "get", "D1")["properties"]}
        return float(p["MaxCurrent"].split()[0]), float(p["SurgeCurrent"].split()[0])
    for model in ("1N4007", "ES1J superfast"):
        c("comp", "set", "D1", "MaxCurrent=2", "SurgeCurrent=5")
        c("comp", "set", "D1", "Model=" + model)
        mx, sg = ratings()
        sim.check("%s: Max Current (A)" % model, mx, 1, tol=1e-6)
        sim.check("%s: Surge Current (A)" % model, sg, 30, tol=1e-6)
    c("comp", "set", "D1", "SurgeCurrent=12")
    path = os.path.join(tempfile.mkdtemp(prefix="diode-"), "d1.sim1")
    c("circuit", "save", path)
    c("circuit", "open", path, "--discard")
    sim.check("SurgeCurrent after save + open (A)", ratings()[1], 12, tol=1e-6)


if __name__ == "__main__":
    run_test(test)
