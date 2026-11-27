"""Power Meter part (docs/POWER_METER.md): a meter in the lead of a source whose other terminal is
ground, the red lead toward the source. It shows the power through it (now, or the mean over
Avg_Time), the r.m.s. voltage at the red lead and the r.m.s. current, and the power factor: the
mean power over (V r.m.s. x I r.m.s.).

Expected values are Ohm's law and the definitions on the test circuits (12 V on 330 ohm in
parallel with 330 ohm: 72.727 mA and 872.73 mW; a 10 V peak sine at 50 Hz: 7.0711 V r.m.s.; a
100 uF capacitor at 50 Hz: 31.83 ohm; 100 ohm in series with it: cos(atan(0.3183)) = 0.95286; a
half-wave rectifier into a resistor with an ideal diode: I r.m.s. = Vm / 2R, P = Vm^2 / 4R and a
power factor of 1 / sqrt(2) = 0.7071, the diode's 0.7 V left out of the expectation and in the
tolerance). Results are [SIMULATED].

    python3 tests/regress/test_power_meter.py              # every section
    python3 tests/regress/test_power_meter.py dc sign      # some sections
"""

import math
import os
import sys
import tempfile

from harness import run_test, SimError

SECTIONS = sys.argv[1:]
V12_R165 = 12.0 / 165.0              # A
P12_R165 = 12.0 * 12.0 / 165.0      # W
VRMS = 10.0 / math.sqrt(2.0)        # The sine's r.m.s.
PF_HALF = 1.0 / math.sqrt(2.0)      # A half-wave rectified sine into a resistor


def want(name):
    return not SECTIONS or name in SECTIONS


def between(sim, name, value, lo, hi):
    """A check that a value lies in [lo, hi] (the table shows the middle and half the width)."""
    return sim.check(name, value, (lo + hi) / 2, absolute=(hi - lo) / 2)


def read(sim, label="PM"):
    r = sim.read(label)
    return r


def build_dc(sim, v="12", r="330", red_to_plus=True):
    """A battery on two resistors in parallel, the meter in the + lead (red toward the battery unless told)."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "BAT", "--at", "0,-40", "Voltage=" + v)
    c("comp", "add", "PowerMeter", "--label", "PM", "--at", "80,0")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "160,-40", "Resistance=" + r)
    c("comp", "add", "Resistor", "--label", "R2", "--at", "160,40", "Resistance=" + r)
    c("comp", "add", "Ground", "--label", "G", "--at", "240,80")
    c("wire", "add", "BAT.lPin", "PM.lPin" if red_to_plus else "PM.rPin")
    c("wire", "add", "PM.rPin" if red_to_plus else "PM.lPin", "R1.lPin")
    c("wire", "add", "PM.rPin" if red_to_plus else "PM.lPin", "R2.lPin")
    c("wire", "add", "R1.rPin", "G.Gnd")
    c("wire", "add", "R2.rPin", "G.Gnd")
    c("wire", "add", "BAT.rPin", "G.Gnd")


def build_ac(sim, load):
    """A 10 V peak 50 Hz sine (20,000 steps a cycle: smooth for a capacitor), the meter in its lead."""
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "WaveGen", "--label", "AC", "--at", "0,0", "Always_On=true", "Freq=50", "Semi_Ampli=10",
      "Mid_Volt=0", "Steps=20000")
    c("comp", "add", "PowerMeter", "--label", "PM", "--at", "80,0", "Reading=Mean")
    c("comp", "add", "Ground", "--label", "G", "--at", "320,80")
    c("wire", "add", "AC.outnod", "PM.lPin")
    if load == "r":
        c("comp", "add", "Resistor", "--label", "R1", "--at", "160,-40", "Resistance=100")
        c("wire", "add", "PM.rPin", "R1.lPin")
        c("wire", "add", "R1.rPin", "G.Gnd")
    elif load == "c":
        c("comp", "add", "Capacitor", "--label", "C1", "--at", "160,0", "Capacitance=100u")
        c("wire", "add", "PM.rPin", "C1.lPin")
        c("wire", "add", "C1.rPin", "G.Gnd")
    elif load == "rc":
        c("comp", "add", "Resistor", "--label", "R1", "--at", "160,-40", "Resistance=100")
        c("comp", "add", "Capacitor", "--label", "C1", "--at", "160,40", "Capacitance=100u")
        c("wire", "add", "PM.rPin", "R1.lPin")
        c("wire", "add", "R1.rPin", "C1.lPin")
        c("wire", "add", "C1.rPin", "G.Gnd")
    elif load == "half":
        c("comp", "set", "AC", "Semi_Ampli=100")
        c("comp", "add", "Diode", "--label", "D1", "--at", "160,-40", "Model=1N4007")
        c("comp", "add", "Resistor", "--label", "R1", "--at", "240,-40", "Resistance=1000")
        c("wire", "add", "PM.rPin", "D1.lPin")
        c("wire", "add", "D1.rPin", "R1.lPin")
        c("wire", "add", "R1.rPin", "G.Gnd")
    elif load == "peak":
        c("comp", "add", "Diode", "--label", "D1", "--at", "160,-40", "Model=1N4007")
        c("comp", "add", "Capacitor", "--label", "C1", "--at", "200,20", "--rot", "270", "Capacitance=100u")
        c("comp", "add", "Resistor", "--label", "R1", "--at", "240,20", "--rot", "270", "Resistance=1000")
        c("wire", "add", "PM.rPin", "D1.lPin")
        c("wire", "add", "D1.rPin", "C1.lPin")
        c("wire", "add", "D1.rPin", "R1.lPin")
        c("wire", "add", "C1.rPin", "G.Gnd")
        c("wire", "add", "R1.rPin", "G.Gnd")


def section_dc(sim):
    c = sim.cli
    build_dc(sim)
    c("sim", "run", "--for", "250ms")
    r = read(sim)
    sim.check("dc: power", r["value"], P12_R165, tol=1e-4)
    sim.check("dc: unit", 1 if r["unit"] == "W" else 0, 1)
    sim.check("dc: reading", 1 if r["reading"] == "Instant" else 0, 1)
    sim.check("dc: over", 1 if r["over"] == "100 ms" else 0, 1)
    sim.check("dc: V r.m.s.", r["v_rms"], 12.0, tol=1e-5)
    sim.check("dc: I r.m.s.", r["i_rms"], V12_R165, tol=1e-4)
    sim.check("dc: apparent power", r["apparent"], P12_R165, tol=1e-4)
    sim.check("dc: power factor", r["power_factor"], 1.0, tol=1e-9)

    # Mean over a block of 100 ms of a steady current: the same
    c("comp", "set", "PM", "Reading=Mean")
    c("sim", "stop")
    c("sim", "run", "--for", "250ms")
    r = read(sim)
    sim.check("dc, Mean: power", r["value"], P12_R165, tol=1e-4)
    sim.check("dc, Mean: reading", 1 if r["reading"] == "Mean" else 0, 1)

    # The readings offered: Instant and Mean (there is no r.m.s. of a power); RMS loads as Mean
    g = c("comp", "get", "PM")
    opts = [p["options"] for p in g["properties"] if p["name"] == "Reading"][0]
    sim.check("readings offered", 1 if opts == ["Instant", "Mean"] else 0, 1)
    try:
        c("comp", "set", "PM", "Reading=RMS")
        refused = False
    except SimError:
        refused = True                                   # "invalid value for Reading: RMS (one of Instant, Mean)"
    sim.check("RMS refused by the CLI", 1 if refused else 0, 1)

    # It goes where an ammeter is: the same pins at the same places, 1 uOhm like it
    c("comp", "add", "Amperimeter", "--label", "AM", "--at", "200,-160")
    pm, am = c("comp", "get", "PM"), c("comp", "get", "AM")
    pp = [(p["id"], p["pos"][0] - pm["pos"][0], p["pos"][1] - pm["pos"][1]) for p in pm["pins"]]
    ap = [(p["id"], p["pos"][0] - am["pos"][0], p["pos"][1] - am["pos"][1]) for p in am["pins"]]
    sim.check("pins where the ammeter's are", 1 if pp == ap else 0, 1)
    c("comp", "remove", "AM")
    v = c("sim", "average", "--for", "50ms", "PM.lPin", "PM.rPin")["channels"]   # Averaged by pin: the meter has three
    dv = abs(v[0]["voltage"]["mean"] - v[1]["voltage"]["mean"])
    i = abs(v[0]["current"]["mean"])
    between(sim, "the meter is 1 uOhm: dV / I", dv / i if i else 99e-6, 0.5e-6, 1.5e-6)
    sim.check("dc: power factor of the block again", read(sim)["power_factor"], 1.0, tol=1e-9)


def section_sign(sim):
    c = sim.cli
    # The source on the black lead: the power flows from the black side to the red side: negative
    build_dc(sim, red_to_plus=False)
    c("sim", "run", "--for", "250ms")
    r = read(sim)
    sim.check("reversed: power", r["value"], -P12_R165, tol=1e-4)
    sim.check("reversed: V r.m.s. is a magnitude", r["v_rms"], 12.0, tol=1e-5)
    sim.check("reversed: I r.m.s. is a magnitude", r["i_rms"], V12_R165, tol=1e-4)
    sim.check("reversed: apparent power is positive", r["apparent"], P12_R165, tol=1e-4)
    sim.check("reversed: power factor", r["power_factor"], -1.0, tol=1e-9)

    # A supply on the other side of ground: the battery's + at ground, the meter in its - lead with
    # the red lead toward the battery. The current runs from the circuit into the battery's -, the
    # voltage at the red lead is -12 V: the battery delivers, so positive
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "BAT", "--at", "0,-40", "Voltage=12")
    c("comp", "add", "PowerMeter", "--label", "PM", "--at", "80,0")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "160,-40", "Resistance=165")
    c("comp", "add", "Ground", "--label", "G", "--at", "240,80")
    c("wire", "add", "BAT.lPin", "G.Gnd")
    c("wire", "add", "BAT.rPin", "PM.lPin")
    c("wire", "add", "PM.rPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "G.Gnd")
    c("sim", "run", "--for", "250ms")
    r = read(sim)
    sim.check("negative supply, red to the battery: power", r["value"], P12_R165, tol=1e-4)
    sim.check("negative supply: power factor", r["power_factor"], 1.0, tol=1e-9)

    # A battery that takes power: 12 V charges 10 V through 10 ohm (0.2 A). The 12 V battery
    # delivers 2.4 W, the 10 V one takes 2.0 W and the resistor 0.4 W
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "0,-80", "Voltage=12")
    c("comp", "add", "Battery", "--label", "B2", "--at", "0,120", "Voltage=10")
    c("comp", "add", "PowerMeter", "--label", "P1", "--at", "80,-80")      # In the + lead of B1
    c("comp", "add", "Resistor", "--label", "R", "--at", "240,-80", "Resistance=10")
    c("comp", "add", "PowerMeter", "--label", "P2", "--at", "240,-160")    # Red at the + of B2
    c("comp", "add", "Ground", "--label", "G", "--at", "400,0")
    c("wire", "add", "B1.lPin", "P1.lPin")
    c("wire", "add", "P1.rPin", "R.lPin")
    c("wire", "add", "R.rPin", "P2.rPin")
    c("wire", "add", "P2.lPin", "B2.lPin")
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B2.rPin", "G.Gnd")
    c("sim", "run", "--for", "100ms")
    r1, r2, rr = read(sim, "P1"), read(sim, "P2"), sim.read("R")
    sim.check("charging: the 12 V battery delivers", r1["value"], 2.4, tol=1e-3)
    sim.check("charging: the 10 V battery takes (negative)", r2["value"], -2.0, tol=1e-3)
    sim.check("charging: the resistor takes what is left", r1["value"] + r2["value"], rr["power"], tol=2e-3)
    sim.check("charging: power factor at the 10 V battery", r2["power_factor"], -1.0, tol=1e-9)


def section_ground(sim):
    # In the ground lead the red pin sits a few 100 nV from ground (the 1 uOhm): no power, and no
    # power factor to give
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "BAT", "--at", "0,-40", "Voltage=12")
    c("comp", "add", "PowerMeter", "--label", "PM", "--at", "80,40")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "160,-40", "Resistance=330")
    c("comp", "add", "Ground", "--label", "G", "--at", "240,80")
    c("wire", "add", "BAT.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "G.Gnd")
    c("wire", "add", "BAT.rPin", "PM.lPin")
    c("wire", "add", "PM.rPin", "G.Gnd")
    c("sim", "run", "--for", "250ms")
    r = read(sim)
    between(sim, "ground lead: power", r["value"], -1e-7, 1e-7)
    sim.check("ground lead: I r.m.s. still read", r["i_rms"], 12.0 / 330.0, tol=1e-3)
    sim.check("ground lead: no power factor", 1 if r["power_factor"] is None else 0, 1)
    sim.check("ground lead: apparent power under 1 uVA", 1 if r["apparent"] < 1e-6 else 0, 1)


def section_ac(sim):
    c = sim.cli
    build_ac(sim, "r")
    c("sim", "run", "--for", "400ms")
    r = read(sim)
    sim.check("ac resistor: power", r["value"], 0.5, tol=1e-4)
    sim.check("ac resistor: V r.m.s.", r["v_rms"], VRMS, tol=1e-5)
    sim.check("ac resistor: I r.m.s.", r["i_rms"], VRMS / 100.0, tol=1e-5)
    sim.check("ac resistor: apparent power", r["apparent"], 0.5, tol=1e-5)
    sim.check("ac resistor: power factor", r["power_factor"], 1.0, tol=1e-6)

    # Instant: p = Vm^2 sin^2(wt) / R, read at pauses on the phases 0, 90, 180 and 225 deg
    c("comp", "set", "PM", "Reading=Instant")
    for span, deg, want_p in [("5ms", 90, 1.0), ("5ms", 180, 0.0), ("2.5ms", 225, 0.5), ("2.5ms", 270, 1.0)]:
        c("sim", "run", "--for", span)
        between(sim, "ac instant at %d deg" % deg, read(sim)["value"], want_p - 1e-3, want_p + 1e-3)
    c("comp", "set", "PM", "Reading=Mean")
    c("sim", "run", "--for", "100ms")                    # The block starts at the change: a whole one, then
    sim.check("ac back to Mean: 0.5 W over a block", read(sim)["value"], 0.5, tol=1e-3)

    # The window: a whole number of cycles gives the same mean; a fifth of a cycle (4 ms, which
    # slides 36 deg a block against the sine) gives the mean of Vm^2 sin^2 / R over 72 deg, which
    # is 0.5 + (sin(2 phi) - sin(2 phi + 144 deg)) / 4. w: between 0.12 and 0.88, and it moves
    c("comp", "set", "PM", "Avg_Time=20ms")
    c("sim", "run", "--for", "100ms")
    r = read(sim)
    sim.check("Avg_Time 20 ms: power", r["value"], 0.5, tol=1e-3)
    sim.check("Avg_Time 20 ms: over", 1 if r["over"] == "20 ms" else 0, 1)
    c("comp", "set", "PM", "Avg_Time=4ms")
    c("sim", "run", "--for", "400ms")
    seen = []
    for _ in range(8):
        c("sim", "run", "--for", "2ms")
        seen.append(read(sim)["value"])
    between(sim, "Avg_Time 4 ms: every block within 0.12 .. 0.88 W", max(seen), 0.12 - 1e-3, 0.88 + 1e-3)
    between(sim, "Avg_Time 4 ms: ... the lowest", min(seen), 0.12 - 1e-3, 0.88 + 1e-3)
    sim.check("Avg_Time 4 ms: the mean moves with the phase (spread over 0.3 W)", 1 if max(seen) - min(seen) > 0.3 else 0, 1)


def section_reactive(sim):
    c = sim.cli
    build_ac(sim, "c")
    c("sim", "run", "--for", "600ms")
    r = read(sim)
    sim.check("capacitor: V r.m.s.", r["v_rms"], VRMS, tol=1e-5)
    sim.check("capacitor: I r.m.s. (V / 31.83 ohm)", r["i_rms"], VRMS * 2 * math.pi * 50 * 100e-6, tol=2e-3)
    sim.check("capacitor: apparent power", r["apparent"], VRMS * VRMS * 2 * math.pi * 50 * 100e-6, tol=2e-3)
    between(sim, "capacitor: no real power", r["value"], -3e-3, 3e-3)
    between(sim, "capacitor: power factor about 0", r["power_factor"], -5e-3, 5e-3)

    build_ac(sim, "rc")
    c("sim", "run", "--for", "600ms")
    r = read(sim)
    z = math.hypot(100.0, 1.0 / (2 * math.pi * 50 * 100e-6))
    sim.check("RC: I r.m.s. (V / |Z|)", r["i_rms"], VRMS / z, tol=1e-3)
    sim.check("RC: power = I^2 R", r["value"], (VRMS / z) ** 2 * 100.0, tol=1e-3)
    sim.check("RC: power = P / S (the meter's own S)", r["value"], r["apparent"] * (100.0 / z), tol=1e-3)
    sim.check("RC: power factor = R / |Z|", r["power_factor"], 100.0 / z, tol=1e-3)
    sim.check("RC: apparent power", r["apparent"], VRMS * VRMS / z, tol=1e-3)


def section_distorted(sim):
    c = sim.cli
    # Half-wave into a resistor: i = Vm sin / R on one half-cycle: I r.m.s. = Vm / 2R,
    # P = Vm^2 / 4R, S = Vm^2 / (2 sqrt 2 R), so the power factor is 1 / sqrt 2
    build_ac(sim, "half")
    c("sim", "run", "--for", "400ms")
    r = read(sim)
    vm, rl = 100.0, 1000.0
    sim.check("half-wave: V r.m.s.", r["v_rms"], vm / math.sqrt(2.0), tol=1e-5)
    sim.check("half-wave: I r.m.s. = Vm / 2R (the diode's 0.7 V in the tolerance)", r["i_rms"], vm / (2 * rl), tol=2.5e-2)
    sim.check("half-wave: power = Vm^2 / 4R", r["value"], vm * vm / (4 * rl), tol=2.5e-2)
    sim.check("half-wave: power factor = 1 / sqrt 2", r["power_factor"], PF_HALF, tol=1e-3)
    sim.check("half-wave: P = pf x S", r["value"], r["power_factor"] * r["apparent"], tol=1e-9)
    c("comp", "set", "AC", "Semi_Ampli=20")
    c("sim", "stop")
    c("sim", "run", "--for", "400ms")
    r = read(sim)
    between(sim, "half-wave at 20 V peak: power (the diode takes 4 %)", r["value"], 0.09, 0.1001)
    sim.check("half-wave at 20 V peak: power factor", r["power_factor"], PF_HALF, tol=2e-3)

    # A rectifier with a capacitor: short pulses of current. Its power factor has no closed form
    # here: it is well under 1, and the r.m.s. current is the simulator's own r.m.s. of the pin
    build_ac(sim, "peak")
    c("sim", "run", "--for", "800ms")
    r = read(sim)
    between(sim, "rectifier with a capacitor: power factor", r["power_factor"], 0.2, 0.9)
    between(sim, "rectifier with a capacitor: power", r["value"], 0.05, 0.12)
    ch = c("sim", "average", "--for", "200ms", "PM.lPin")["channels"][0]
    sim.check("rectifier with a capacitor: I r.m.s. = the pin's r.m.s.", r["i_rms"], abs(ch["current"]["rms"]), tol=2e-3)
    sim.check("rectifier with a capacitor: V r.m.s. = the pin's r.m.s.", r["v_rms"], ch["voltage"]["rms"], tol=1e-5)
    sim.check("rectifier with a capacitor: S = V x I", r["apparent"], r["v_rms"] * r["i_rms"], tol=1e-9)


def section_output(sim):
    # The output pin gives the power as a voltage (1 V = 1 W), wired or not
    c = sim.cli
    build_dc(sim)
    c("comp", "add", "Resistor", "--label", "RO", "--at", "160,-160", "Resistance=1000")
    c("wire", "add", "PM.outnod", "RO.lPin")
    c("wire", "add", "RO.rPin", "G.Gnd")
    c("sim", "run", "--for", "250ms")
    sim.check("output pin: 1 V = 1 W (Instant)", sim.read("RO")["voltage"], read(sim)["value"], tol=1e-6)
    c("comp", "set", "PM", "Reading=Mean")
    c("sim", "stop")
    c("sim", "run", "--for", "250ms")
    sim.check("output pin: 1 V = 1 W (Mean)", sim.read("RO")["voltage"], read(sim)["value"], tol=1e-6)


def section_save(sim):
    c = sim.cli
    build_dc(sim)
    c("comp", "set", "PM", "Reading=Mean", "Avg_Time=40ms")
    fd, path = tempfile.mkstemp(suffix=".sim1")
    os.close(fd)
    try:
        c("sim", "stop")
        c("circuit", "save", path)
        with open(path, encoding="utf-8") as f:          # A file that says RMS (no reading of this meter): Mean
            text = f.read().replace('Reading="Mean"', 'Reading="RMS"')
        with open(path, "w", encoding="utf-8") as f:
            f.write(text)
        sim.check("the file held a Reading", 1 if 'Reading="RMS"' in text else 0, 1)
        c("circuit", "open", path, "--discard")
        g = c("comp", "get", "PM")
        props = {q["name"]: q["value"] for q in g["properties"] if q["name"] in ("Reading", "Avg_Time")}
        sim.check("save and reopen: still a PowerMeter", 1 if g["type"] == "PowerMeter" else 0, 1)
        sim.check("RMS in a file loads as Mean (its Reading property)", 1 if props.get("Reading") == "Mean" else 0, 1)
        sim.check("save and reopen: Avg_Time kept", 1 if props.get("Avg_Time") == "40 ms" else 0, 1)
        c("sim", "run", "--for", "250ms")
        r = read(sim)
        sim.check("save and reopen: it measures again", r["value"], P12_R165, tol=1e-4)
        sim.check("save and reopen: the reading is Mean", 1 if r["reading"] == "Mean" else 0, 1)
        sim.check("save and reopen: the window kept", 1 if r["over"] == "40 ms" else 0, 1)
    finally:
        try:
            os.remove(path)
        except OSError:
            pass


def section_back(sim):
    # State code (specs/sim-control.md §4.8): the history is usable with the part, and going back
    # gives the same state again
    c = sim.cli
    build_ac(sim, "rc")
    c("sim", "run", "--for", "200ms")
    h = c("sim", "history")
    sim.check("the part has state code (the history is usable)", 1 if h["usable"] else 0, 1)
    k = c("sim", "history", "check", "--span", "1ms", "--points", "3", "--ago", "4ms", timeout=300)
    sim.check("identical after going back", 1 if k["ok"] else 0, 1)
    # The reading changed during a run (the block starts there): going back within what ran
    # since then (a key frame from before the change would hold the old reading) is identical too
    c("comp", "set", "PM", "Reading=Instant")
    c("sim", "run", "--for", "100ms")
    before = c("sim", "status")["time_ps"]
    k = c("sim", "history", "check", "--span", "1ms", "--points", "3", "--ago", "4ms", timeout=300)
    sim.check("identical after going back with the reading changed", 1 if k["ok"] else 0, 1)
    sim.check("the run went on from where it was", 1 if int(c("sim", "status")["time_ps"]) >= int(before) else 0, 1)


def test(sim):
    for name, fn in [("dc", section_dc), ("sign", section_sign), ("ground", section_ground), ("ac", section_ac),
                     ("reactive", section_reactive), ("distorted", section_distorted), ("output", section_output),
                     ("save", section_save), ("back", section_back)]:
        if want(name):
            fn(sim)


if __name__ == "__main__":
    run_test(test)
