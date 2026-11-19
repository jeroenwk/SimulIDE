#!/usr/bin/env python3
"""Cost of the pieces a fuse would add, measured on a headless SimulIDE.

For each test circuit, the place where a fuse would go is either a plain wire (base) or a
0.1 Ohm resistor (what a fuse costs as a two-pin linear part: one more node). Wall time per
simulated span, best of N runs.
"""
import os, sys, time
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "tools"))
from simctl import Sim

sim = Sim(socket=os.environ.get("SIMULIDE_SOCKET", "fusebench"), verbose=False)
N = 3
SPEED = "1000000"
pos = [0]


def add(t, **p):
    pos[0] += 1
    r = sim.cli("comp", "add", t, "--at", "%d,%d" % (pos[0] * 64, (pos[0] % 5) * 64),
                *["%s=%s" % kv for kv in p.items()])
    return r["label"]


def w(a, b):
    sim.cli("wire", "add", a, b)


def series(a, b, fuse):
    """Join a and b with a wire, or through a 0.1 Ohm resistor (a fuse-like part)."""
    if fuse == "wire":
        w(a, b)
    else:
        r = add("Resistor", Resistance="0.1")
        w(a, r + ".lPin"); w(r + ".rPin", b)


def build(kind, fuse, tick=False):
    sim.cli("circuit", "new", "--discard")
    pos[0] = 0
    g = add("Ground")
    if kind == "quiet":
        b = add("Battery", Voltage="12")
        r = add("Resistor", Resistance="100")
        series(b + ".rPin", r + ".lPin", fuse)
        w(r + ".rPin", g + ".Gnd"); w(b + ".lPin", g + ".Gnd")
    else:
        if kind == "rect50":
            s = add("WaveGen", Wave_Type="Sine", Freq="50Hz", Semi_Ampli="15", Always_On="true")
            load, cap = "47", "1000"
        else:   # sw100k
            s = add("WaveGen", Wave_Type="Square", Freq="100kHz", Semi_Ampli="5", Always_On="true")
            load, cap = "100", "10"
        d = add("Diode")
        c = add("Capacitor", Capacitance=cap + "µF")
        rl = add("Resistor", Resistance=load)
        if kind == "sw100k":
            rs = add("Resistor", Resistance="10")
            series(s + ".outnod", rs + ".lPin", fuse)
            w(rs + ".rPin", d + ".lPin")
        else:
            series(s + ".outnod", d + ".lPin", fuse)
        w(d + ".rPin", c + ".lPin"); w(c + ".lPin", rl + ".lPin")
        w(c + ".rPin", g + ".Gnd"); w(rl + ".rPin", g + ".Gnd")
    if tick:   # something that steps every 0.5 us: upper bound for a fuse that polls
        k = add("WaveGen", Wave_Type="Square", Freq="1MHz", Semi_Ampli="1", Always_On="true")
        rk = add("Resistor", Resistance="1k")
        g2 = add("Ground")
        w(k + ".outnod", rk + ".lPin"); w(rk + ".rPin", g2 + ".Gnd")


def bench(kind, fuse, span, tick=False):
    build(kind, fuse, tick)
    sim.cli("sim", "start")
    sim.cli("sim", "speed", SPEED)
    sim.cli("sim", "run", "--for", span)          # warm-up
    best = 1e9
    for _ in range(N):
        t0 = time.perf_counter()
        sim.cli("sim", "run", "--for", span)
        best = min(best, time.perf_counter() - t0)
    info = sim.cli("circuit", "info")
    sim.cli("sim", "stop")
    return best, info.get("matrix", "")


if __name__ == "__main__":
    CASES = [("quiet", "100s"), ("rect50", "10s"), ("sw100k", "500ms")]
    res = {}
    for kind, span in CASES:
        for fuse in ("wire", "res"):
            t, m = bench(kind, fuse, span)
            res[(kind, fuse)] = t
            print("%-7s %-5s %-5s %8.3f s wall   %s" % (kind, fuse, span, t, str(m)[:160]), flush=True)
        print("   -> resistor in place of wire: %+.1f %%" %
              (100 * (res[(kind, "res")] / res[(kind, "wire")] - 1)), flush=True)
    t, m = bench("quiet", "wire", "100s", tick=True)
    print("quiet   +1MHz tick 100s %8.3f s wall (vs %.3f s without)" % (t, res[("quiet", "wire")]))
