"""LED ratings as in a datasheet: Max Current is IF (an average), Peak Current is IFP (a peak).

Before, the LED state compared the current at that moment with Max Current: the LED strip of
led_driver board_mains (5 strings of 12, 312 mA average, 550 mA peaks at 100 Hz from the PFC
driver) read OVER on every mains crest, although each LED averaged 62 mA. Now the current goes
through a 100 ms first-order low-pass and is compared with Max Current (OVER, back below 0.95 x),
and the current of each solved step with the new Peak Current (PEAK, held 100 ms after the last
step above it; 0 = none, the default).

Right results:
- 55 mA DC into a 30 mA LED: the average after 50 ms is I (1 - e^-0.5), still ON; after 500 ms
  it is I and the state is OVER;
- a 50 Hz sine of 0..50 mA into a 30 mA LED: the average is the mean (within 10 %), ON, no OVER
  (the old code read OVER on every crest);
- 75 mA for 250 ms (2 Hz square), Peak Current 60 mA: PEAK, current_peak = I; still PEAK 50 ms
  after the current stops, OFF 150 ms after;
- a strip of 5 x 12 LEDs, 60 mA IF and 100 mA IFP per LED, on a 100 Hz ripple of ~295 mA mean and
  ~565 mA peaks: PEAK (113 mA per LED); with IFP 120 mA: ON (the ratings scale with the strings);
- PeakCurrent and LedIpeak survive save and open.
"""
import math
import os
import tempfile
from harness import run_test

TAU = 0.1


def dc(sim, volt, r):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=%g" % volt)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "48,-48", "Resistance=%g" % r)
    c("comp", "add", "Led", "--label", "D1", "--at", "96,0", "--rot", "-90", "Show_State=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,96")
    c("wire", "add", "B1.lPin", "R1.lPin"); c("wire", "add", "R1.rPin", "D1.lPin")
    c("wire", "add", "D1.rPin", "G.Gnd"); c("wire", "add", "B1.rPin", "G.Gnd")


def wave(sim, part, *props, r):
    """WaveGen (driving against ground) -> R1 -> D1 (an LED or a strip) -> ground."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "W1", "--at", "0,0", "Running=true", "Out=true", *props)
    c("comp", "add", "Resistor", "--label", "R1", "--at", "48,-48", "Resistance=%g" % r)
    c("comp", "add", part, "--label", "D1", "--at", "96,0", "--rot", "-90", "Show_State=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,96")
    c("wire", "add", "W1.outnod", "R1.lPin"); c("wire", "add", "R1.rPin", "D1.lPin")
    c("wire", "add", "D1.rPin", "G.Gnd")


def props(sim):
    return {q["name"]: q["value"] for q in sim.cli("comp", "get", "D1", "--all")["properties"]}


def ma(value):
    """'60 mA' -> 0.06"""
    v, unit = value.split()[:2]
    return float(v) * {"mA": 1e-3, "A": 1, "µA": 1e-6}[unit]


def test(sim):
    c = sim.cli
    dc(sim, 5, 47)                                      # (5 - 2.4) / 47.6 = 55 mA, Max Current 30 mA
    c("comp", "set", "D1", "MaxCurrent=30mA")
    sim.run("50ms")
    d = sim.read("D1")
    i = c("sim", "average", "--for", "1ms", "D1")["channels"][0]["current"]["mean"]
    sim.check("DC: current (A)", i, 0.055, tol=0.05)
    sim.check("DC, 50 ms: average = I (1 - e^-0.5) (A)", d["current_avg"], i * (1 - math.exp(-0.05 / TAU)), tol=0.02)
    sim.check("DC, 50 ms: not OVER yet", int(d["state"] == "ON"), 1, absolute=0, note=d["state"])
    sim.run("450ms")
    d = sim.read("D1")
    sim.check("DC, 500 ms: average = I (A)", d["current_avg"], i * (1 - math.exp(-0.5 / TAU)), tol=0.02)
    sim.check("DC, 500 ms: OVER (55 mA average > 30 mA)", int(d["state"] == "OVER"), 1, absolute=0, note=d["state"])
    sim.check("DC, 500 ms: overloaded", int(d["overloaded"]), 1, absolute=0)
    c("sim", "stop")

    # 0..50 mA sine at 50 Hz, mean ~25 mA, into a 30 mA LED: above IF at every crest, not on average
    wave(sim, "Led", "Wave_Type=Sine", "Freq=50Hz", "Semi_Ampli=2.5", "Mid_Volt=4.9", r=100)
    c("comp", "set", "D1", "MaxCurrent=30mA")
    sim.run("500ms")
    avg = c("sim", "average", "--for", "200ms", "D1")["channels"][0]["current"]
    d = sim.read("D1")
    sim.check("sine: peak current (A)", avg["max"], 0.05, tol=0.1)
    sim.check("sine: average = mean I (A)", d["current_avg"], avg["mean"], tol=0.10)
    sim.check("sine: ON, no OVER (crests above IF, average below)", int(d["state"] == "ON"), 1, absolute=0,
              note=d["state"])
    sim.check("sine: not overloaded", int(d["overloaded"]), 0, absolute=0)
    sim.check("sine: current_peak = the largest I (A)", d["current_peak"], avg["max"], tol=0.02)
    c("sim", "stop")

    # 75 mA for 250 ms, then nothing (2 Hz square), Peak Current 60 mA, only the peak rating in play
    wave(sim, "Led", "Wave_Type=Square", "Freq=2Hz", "Semi_Ampli=5", "Mid_Volt=5", r=100)
    c("comp", "set", "D1", "MaxCurrent=1000mA", "PeakCurrent=60mA")
    sim.run("100ms")
    d = sim.read("D1")
    sim.check("75 mA, IFP 60 mA: PEAK", int(d["state"] == "PEAK"), 1, absolute=0, note=d["state"])
    sim.check("75 mA: current_peak (A)", d["current_peak"], 0.0755, tol=0.03)
    sim.run("200ms")                                    # 300 ms: off for 50 ms
    d = sim.read("D1")
    sim.check("off for 50 ms: still PEAK (held 100 ms)", int(d["state"] == "PEAK"), 1, absolute=0, note=d["state"])
    sim.run("100ms")                                    # 400 ms: off for 150 ms
    d = sim.read("D1")
    sim.check("off for 150 ms: OFF", int(d["state"] == "OFF"), 1, absolute=0, note=d["state"])
    c("sim", "stop")
    c("comp", "set", "D1", "PeakCurrent=0")             # None: the same drive stays ON
    sim.run("100ms")
    d = sim.read("D1")
    sim.check("75 mA, no IFP: ON", int(d["state"] == "ON"), 1, absolute=0, note=d["state"])
    c("sim", "stop")

    # The led_driver lamp: 5 strings of 12, per LED 2.70 V + 4.95 Ohm (32.4 V + 11.88 Ohm), IF 60 mA,
    # IFP 100 mA. A 100 Hz ripple of 35.9 +- 3.2 V straight on it: ~295 mA mean (59 mA per LED,
    # below IF), ~565 mA peaks (113 mA per LED, above IFP)
    wave(sim, "LedStrip", "Wave_Type=Sine", "Freq=100Hz", "Semi_Ampli=3.2", "Mid_Volt=35.9", r=0.001)
    c("comp", "set", "D1", "Strings=5", "LedVf=2.7", "LedRes=4.95", "LedImax=60mA", "LedIpeak=100mA")
    sim.run("500ms")
    avg = c("sim", "average", "--for", "100ms", "D1")["channels"][0]["current"]
    d = sim.read("D1")
    sim.check("strip: mean current (A)", avg["mean"], 0.295, tol=0.03)
    sim.check("strip: peak current (A)", avg["max"], 0.564, tol=0.03)
    sim.check("strip, IFP 5 x 100 mA: PEAK", int(d["state"] == "PEAK"), 1, absolute=0, note=d["state"])
    sim.check("strip: not overloaded (average < 5 x 60 mA)", int(d["overloaded"]), 0, absolute=0)
    c("sim", "stop")
    c("comp", "set", "D1", "LedIpeak=120mA")
    sim.run("300ms")
    d = sim.read("D1")
    sim.check("strip, IFP 5 x 120 mA: ON", int(d["state"] == "ON"), 1, absolute=0, note=d["state"])
    c("sim", "stop")

    path = os.path.join(tempfile.mkdtemp(prefix="led-"), "strip.sim1")
    c("circuit", "save", path)
    c("circuit", "open", path, "--discard")
    sim.check("LedIpeak after save + open (A)", ma(props(sim)["LedIpeak"]), 0.12, tol=1e-6)
    dc(sim, 5, 47)
    c("comp", "set", "D1", "PeakCurrent=45mA")
    c("circuit", "save", path)
    c("circuit", "open", path, "--discard")
    sim.check("PeakCurrent after save + open (A)", ma(props(sim)["PeakCurrent"]), 0.045, tol=1e-6)


if __name__ == "__main__":
    run_test(test)
