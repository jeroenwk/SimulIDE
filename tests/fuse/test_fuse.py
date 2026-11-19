#!/usr/bin/env python3
"""Fuse part (specs/fuse.md § 8): blow times, I²t, cooling, AC, replacing, warnings, resistor
mode and save/load, each against its own headless SimulIDE (tests/regress/harness.py).

Expected values come from fuse_ref.py, an RK4 integration of the heat equations of
specs/fuse.md § 4 (the C++ part solves them in closed form), from the IEC 60127 time windows
quoted by the Schurter datasheets (§ 3.2), and from circuit formulas. Results are [SIMULATED].

    python3 tests/fuse/test_fuse.py              # every section
    python3 tests/fuse/test_fuse.py gates pulse  # some sections
"""
import math
import os
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "regress"))
from harness import run_test  # noqa: E402
from fuse_ref import Heat, VDROP_1A  # noqa: E402

# IEC 60127-2 pre-arcing windows (s) for 0.125-6.3 A  [DOC Schurter FSF / FST datasheets]
IEC = {"F": {1.5: (3600, None), 2.1: (None, 1800), 2.75: (0.05, 2), 4: (0.01, 0.3), 10: (None, 0.02)},
       "T": {1.5: (3600, None), 2.1: (None, 120), 2.75: (0.6, 10), 4: (0.15, 3), 10: (0.02, 0.3)}}

SECTIONS = sys.argv[1:]


def want(name):
    return not SECTIONS or name in SECTIONS


def secs(t):
    return "%.9fs" % t


def parse_time(txt):
    """'12.234 s', '35.7 ms', '4 µs' (status history) -> seconds."""
    num, unit = txt.split()[:2]
    return float(num) * {"s": 1, "ms": 1e-3, "µs": 1e-6, "us": 1e-6, "ns": 1e-9, "ps": 1e-12}[unit]


def r(sim, comp="F1"):
    return sim.read(comp)


def flag(sim, name, cond):
    sim.check(name, 1 if cond else 0, 1, absolute=0)


def build_dc(sim, volts, r_load, speed="T", source="battery", cap=None, **fuse):
    """Battery (or WaveGen) - switch SW - fuse F1 - RL [- C1] - ground. SW closed."""
    sim.cli("circuit", "new", "--discard")
    sim.cli("sim", "speed", "1000000")
    c = sim.cli
    if source == "battery":
        c("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=%g" % volts)
        top, bot = "B1.rPin", "B1.lPin"
    else:
        c("comp", "add", "WaveGen", "--label", "B1", "--at", "0,0", "Always_On=true", *source)
        top, bot = "B1.outnod", None
    c("comp", "add", "Switch", "--label", "SW", "--at", "48,-48")
    c("comp", "add", "Fuse", "--label", "F1", "--at", "112,-48", "Speed=" + speed,
      *["%s=%s" % kv for kv in fuse.items()])
    c("comp", "add", "Resistor", "--label", "RL", "--at", "176,0", "--rot", "90", "Resistance=%g" % r_load)
    c("comp", "add", "Ground", "--label", "G", "--at", "0,96")
    c("wire", "add", top, "SW.pinP0")
    c("wire", "add", "SW.switch0pinN", "F1.lPin")
    c("wire", "add", "F1.rPin", "RL.lPin")
    if cap:
        c("comp", "add", "Capacitor", "--label", "C1", "--at", "176,64", "--rot", "90", "Capacitance=%gµF" % cap)
        c("wire", "add", "RL.rPin", "C1.lPin")
        c("wire", "add", "C1.rPin", "G.Gnd")
    else:
        c("wire", "add", "RL.rPin", "G.Gnd")
    if bot:
        c("wire", "add", bot, "G.Gnd")
    c("ui", "set", "SW", "on")


def load_for(mult, volts, speed):
    """Load resistance for mult x In (1 A) from volts: battery 1 mOhm, fuse Vdrop/In."""
    return volts / mult - VDROP_1A[speed] - 0.001


# ---- 1. Time-current limits ------------------------------------------------------------------
def gates(sim):
    for speed in ("F", "T"):
        for mult in (1.5, 2.1, 2.75, 4, 10):
            build_dc(sim, 30, load_for(mult, 30, speed), speed)
            sim.run("1ms")
            i = abs(r(sim)["current"])
            ref = Heat(speed).melt_time(i * i, 4000)
            lo, hi = IEC[speed][mult]
            name = "%s %.2f In" % (speed, mult)
            if ref is None:                     # 1.5 In: must not blow within 1 h
                sim.run("3600s")
                flag(sim, name + ": not blown in 1 h", not r(sim)["blown"])
                continue
            sim.run(secs(ref * 1.01 + 1e-3))
            st = r(sim)
            sim.check(name + ": blow time (s)", st.get("blown_at"), ref, tol=0.002)
            t = st.get("blown_at") or 0
            flag(sim, name + ": inside IEC window", (lo is None or t >= lo) and (hi is None or t <= hi))


# ---- 2. I²t: a pulse just shorter / longer than the melting time ------------------------------
def pulse(sim):
    for k, blows in ((0.95, False), (1.05, True)):
        build_dc(sim, 30, load_for(10, 30, "T"), "T")
        sim.run("1ms")
        i = abs(r(sim)["current"])
        tm = Heat("T").melt_time(i * i, 10)
        sim.run(secs(tm * k - 1e-3))
        sim.cli("ui", "set", "SW", "off")
        sim.run("1s")
        st = r(sim)
        flag(sim, "T 10 In pulse %.2f x t_melt: %s" % (k, "blown" if blows else "not blown"), st["blown"] == blows)
        if not blows:
            h = Heat("T")
            h.run(lambda t: i * i, tm * k, tm / 2000)
            h.run(lambda t: 0.0, 1.0, 1e-3)
            sim.check("T heat 1 s after a 0.95 pulse (%)", st["heat"], 100 * h.th, tol=0.01)


# ---- 3. Inrush: 325 V switched into 10 Ohm + 470 uF ----------------------------------------
def inrush(sim):
    v, rl, cap = 325.0, 10.0, 470.0
    for speed, blows in (("T", False), ("F", True)):
        build_dc(sim, v, rl, speed, cap=cap)
        sim.run("200ms")
        st = r(sim)
        rt = rl + VDROP_1A[speed] + 0.001
        i2t = v * v * cap * 1e-6 / (2 * rt)            # ∫(V/R e^-t/RC)² dt = V²C/2R
        flag(sim, "inrush %s 1A: %s" % (speed, "blown" if blows else "survives"), st["blown"] == blows)
        if not blows:
            sim.check("inrush I²t (A²s)", st["i2t"], i2t, tol=0.02)
            h = Heat(speed)
            tau = rt * cap * 1e-6
            h.run(lambda t: (v / rt) ** 2 * math.exp(-2 * t / tau), 0.2, tau / 500)
            sim.check("inrush heat after 200 ms (%)", st["heat"], 100 * h.th, tol=0.02)


# ---- 4. Pulse trains: heat adds up, and cools between pulses ---------------------------------
def train(sim):
    # 3 x In, 50 Hz square, 50 % duty: mean I² = 4.5 In² > I_mf² = 4 In² (T): blows
    wave = ["Wave_Type=Square", "Freq=50Hz", "Semi_Ampli=15", "Mid_Volt=15", "Duty=50"]
    build_dc(sim, 30, load_for(3, 30, "T"), "T", source=wave)
    sim.run("1ms")
    i = abs(r(sim)["current"])
    sim.run("60s")
    st = r(sim)
    h = Heat("T")
    ref = h.run(lambda t: i * i if (t % 0.02) < 0.01 else 0.0, 60, 2e-4)
    flag(sim, "50 Hz train at 3 In, 50 %: blown", st["blown"])
    sim.check("50 Hz train: blow time (s)", st.get("blown_at"), ref or 0, tol=0.02,
              note="square phase and edges: 2 %")
    # Same pulses 2 s apart (0.5 %): never blows
    wave = ["Wave_Type=Square", "Freq=0.5Hz", "Semi_Ampli=15", "Mid_Volt=15", "Duty=0.5"]
    build_dc(sim, 30, load_for(3, 30, "T"), "T", source=wave)
    sim.run("60s")
    flag(sim, "0.5 Hz train, 10 ms pulses: not blown in 60 s", not r(sim)["blown"])


# ---- 5. Cooling in a quiet circuit ------------------------------------------------------------
def cooling(sim):
    build_dc(sim, 30, load_for(3, 30, "T"), "T")
    sim.run("1ms")
    i = abs(r(sim)["current"])
    sim.run(secs(0.5 - 1e-3))
    sim.cli("ui", "set", "SW", "off")
    sim.run("10s")
    h = Heat("T")
    h.run(lambda t: i * i, 0.5, 1e-4)
    h.run(lambda t: 0.0, 10, 1e-3)
    sim.check("T heat after 0.5 s at 3 In + 10 s off (%)", r(sim)["heat"], 100 * h.th, tol=0.005)


# ---- 6. AC: 50 Hz sine at 3 In rms against DC at 3 In ----------------------------------------
def ac(sim):
    amp = 30 * math.sqrt(2)
    wave = ["Wave_Type=Sine", "Freq=50Hz", "Semi_Ampli=%g" % amp, "Mid_Volt=0", "Steps=200"]
    build_dc(sim, 30, load_for(3, 30, "T"), "T", source=wave)
    sim.run("3s")
    st = r(sim)
    ipk = amp / (load_for(3, 30, "T") + VDROP_1A["T"])
    dc = Heat("T").melt_time((ipk / math.sqrt(2)) ** 2, 10)
    ref = Heat("T").run(lambda t: (ipk * math.sin(2 * math.pi * 50 * t)) ** 2, 10, 1e-4)
    flag(sim, "AC 3 In rms: blown", st["blown"])
    sim.check("AC blow time against sine reference (s)", st.get("blown_at"), ref or 0, tol=0.01,
              note="WaveGen sine in 200 steps")
    sim.check("AC blow time against DC at the same rms (s)", st.get("blown_at"), dc, tol=0.05)


# ---- 5b. States from the heat, not the instantaneous current ---------------------------------
def states(sim):
    # 100 kHz pulses of 3 In at 10 % duty: rms 0.95 In, like a switching load's input current.
    # HOT must not follow the pulses (it did: OK <-> HOT at 85 kHz in led_driver board_mains)
    wave = ["Wave_Type=Square", "Freq=100kHz", "Semi_Ampli=15", "Mid_Volt=15", "Duty=10"]
    build_dc(sim, 30, load_for(3, 30, "T"), "T", source=wave)
    sim.run("2s")
    hist = sim.cli("status", "F1").get("history", [])
    flag(sim, "3 In pulses at 10 %% (0.95 In rms): only OK (%d changes)" % len(hist),
         [h.get("text") for h in hist] == ["OK"])
    # 1.2 In steady, a quiet circuit: θ rises past the heat of In (T: 0.25, steady 0.36) slowly,
    # with the body, and the state must change at that time with no other change in the circuit
    build_dc(sim, 30, load_for(1.2, 30, "T"), "T")
    sim.run("1ms")
    i = abs(r(sim)["current"])
    sim.run("20s")
    hist = sim.cli("status", "F1").get("history", [])
    flag(sim, "1.2 In steady: OK then HOT, once", [h.get("text") for h in hist] == ["OK", "HOT"])
    h = Heat("T")
    h = Heat("T"); t = 0.0
    while h.th < 0.25 and t < 30:
        h.step(1e-3, i * i); t += 1e-3
    got = parse_time(hist[-1].get("time", "")) if hist else None
    sim.check("1.2 In: HOT at (s)", got, t, tol=0.002, note="reference: θ reaches 0.25")
    # 10 In: an overload, so an event is already pending at the adiabatic melting bound (~32 ms);
    # HOT must still come when θ reaches 0.25 (~9 ms), not at that event (it did, smoke test)
    build_dc(sim, 30, load_for(10, 30, "T"), "T")
    sim.run("1ms")
    i = abs(r(sim)["current"])
    sim.run("100ms")
    hist = sim.cli("status", "F1").get("history", [])
    texts = [x.get("text") for x in hist]
    flag(sim, "10 In: OK, HOT, BLOWN (%s)" % texts, texts == ["OK", "HOT", "BLOWN"])
    h = Heat("T"); t = 0.0
    while h.th < 0.25 and t < 1:
        h.step(1e-6, i * i); t += 1e-6
    got = parse_time(hist[1].get("time", "")) if len(hist) > 1 else None
    sim.check("10 In: HOT at (s)", got, t, tol=0.002, note="reference: θ reaches 0.25")


# ---- 7/8. Replace, restart, KeepBlown -------------------------------------------------------
def replace(sim):
    build_dc(sim, 30, load_for(10, 30, "T"), "T")
    sim.run("100ms")
    flag(sim, "blown at 10 In", r(sim)["blown"])
    sim.cli("comp", "set", "F1", "Blown=false")
    sim.run("1ms")
    st = r(sim)
    flag(sim, "replaced while running: conducts", not st["blown"] and abs(st["current"]) > 9)
    sim.run("100ms")
    flag(sim, "replaced fuse blows again", r(sim)["blown"])
    sim.cli("sim", "stop")
    sim.run("1ms")
    flag(sim, "restart: a fresh fuse", not r(sim)["blown"])
    sim.cli("comp", "set", "F1", "KeepBlown=true")
    sim.run("100ms")
    sim.cli("sim", "stop")
    sim.run("1ms")
    st = r(sim)
    flag(sim, "KeepBlown: still blown after a restart", st["blown"] and abs(st["current"]) < 1e-6)
    sim.cli("comp", "set", "F1", "KeepBlown=false", "Blown=false")
    sim.cli("comp", "set", "F1", "Blown=true")       # Blown by hand while running
    flag(sim, "blown by hand", r(sim)["blown"])


# ---- 9. Ratings ----------------------------------------------------------------------------
def ratings(sim):
    build_dc(sim, 30, 0.1, "T")                        # ~190 A > 35 A
    sim.run("100ms")
    st = r(sim)
    flag(sim, "short circuit: BLOWN, BROKE", st.get("state") == "BLOWN, BROKE")
    build_dc(sim, 400, load_for(10, 400, "T"), "T")    # 400 V across a blown 250 V fuse
    sim.run("200ms")
    msgs = sim.cli("messages")
    lines = msgs.get("lines", msgs) if isinstance(msgs, dict) else msgs
    text = " ".join(lines) if isinstance(lines, list) else str(lines)
    flag(sim, "400 V across a blown 250 V fuse: Messages note", "above its rating" in text)
    flag(sim, "blow line in Messages", "F1: blew" in text)


# ---- 10. Resistor mode --------------------------------------------------------------------------
def resistor(sim):
    build_dc(sim, 30, load_for(100, 30, "T"), "T", Model="Resistor")
    sim.run("2s")
    st = r(sim)
    flag(sim, "resistor mode: never blows at 100 In", "blown" not in st and abs(st["current"]) > 90)
    sim.check("resistor mode: V = I R", st["voltage"], st["current"] * VDROP_1A["T"], tol=1e-6)


# ---- 11. Save and reopen ---------------------------------------------------------------------
def saveload(sim):
    props = {"Current": "500 mA", "Speed": "F", "I2t": "0.2 A²s", "Vdrop": "150 mV", "Voltage": "125 V",
             "Breaking": "50 A", "KeepBlown": "true", "MinFuse": "1.8 ×In", "Share": "0.5",
             "Tau2": "100 s", "Model": "Resistor"}
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Fuse", "--label", "F1", "--at", "0,0")
    sim.cli("comp", "set", "F1", *["%s=%s" % (k, v.split(" ")[0] + (v.split(" ")[1] if " " in v else ""))
                                   for k, v in props.items()])
    path = os.path.join(tempfile.mkdtemp(), "fuse_roundtrip.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("circuit", "open", path, "--discard")
    got = {p["name"]: p["value"] for p in sim.cli("comp", "get", "F1")["properties"]}
    for k, v in props.items():
        flag(sim, "round trip %s = %s (got %s)" % (k, v, got.get(k)), got.get(k) == v)
    labels = sim.cli("comp", "get", "F1").get("labels", [])
    flag(sim, "value label F500mA", any(l.get("text") == "F500mA" for l in labels))


def test(sim):
    for name, fn in (("gates", gates), ("pulse", pulse), ("inrush", inrush), ("train", train),
                     ("cooling", cooling), ("states", states), ("ac", ac), ("replace", replace), ("ratings", ratings),
                     ("resistor", resistor), ("saveload", saveload)):
        if want(name):
            sim.log("== %s" % name)
            fn(sim)


if __name__ == "__main__":
    run_test(test)
