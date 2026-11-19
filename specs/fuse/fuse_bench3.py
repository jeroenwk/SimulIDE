"""Stand-ins for the fuse designs, in the place where a fuse would go:
  wire     no part (reference)
  res      Resistor 0.1 Ohm: the fuse as a plain resistor (one more node)
  res+cb   Resistor + Voltmeter across it reading RMS: a callback and an integration at every
           settled step that changes its nodes (the event-free fuse design)
  lamp-fix Lamp with RatedPower 0 (fixed resistance, no thermal model)
  lamp-th  Lamp with its filament model on: a polling event every 20 us (the naive design)
  fuse     the Fuse part (T 10 A, Vdrop 1 V: 0.1 Ohm like res; never near blowing here)
  fuse-res the Fuse part with Model = Resistor

Usage (a headless SimulIDE on socket "fusebench", or SIMULIDE_SOCKET):
  python3 fuse_bench3.py "quiet:100s,rect50:20s,sw100k:2s,rect50+big:10s,sw100k+big:1s" wire,res,res+cb,lamp-fix,lamp-th
"""
import os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__))); import fuse_bench as fb
sim = fb.sim
fb.N = 3

def series(a, b, fuse):
    if fuse == "wire":
        fb.w(a, b); return
    if fuse.startswith("fuse"):
        r = fb.add("Fuse", Current="10", Vdrop="1000mV", Model="Resistor" if fuse == "fuse-res" else "Fuse")
        fb.w(a, r + ".lPin"); fb.w(r + ".rPin", b)
        return
    if fuse.startswith("lamp"):
        p = dict(Resistance="0.1", RatedPower="0") if fuse == "lamp-fix" else \
            dict(RatedVolt="0.2", RatedPower="2", ColdRatio="1.5", ThermalTau="0.05")
        r = fb.add("Lamp", **p)
    else:
        r = fb.add("Resistor", Resistance="0.1")
    fb.w(a, r + ".lPin"); fb.w(r + ".rPin", b)
    if fuse == "res+cb":
        v = fb.add("Voltimeter", Reading="RMS")
        fb.w(r + ".lPin", v + ".lPin"); fb.w(r + ".rPin", v + ".rPin")
fb.series = series

_build = fb.build
def build(kind, fuse, tick=False):
    big = kind.endswith("+big")
    _build(kind.replace("+big", ""), fuse, tick)
    if big:   # 30 more nodes in the same solver group: a ladder on the source side
        labels = [c["label"] for c in sim.cli("comp", "list", "--type", "Diode")]
        d = labels[0]; prev = d + ".lPin"
        g = fb.add("Ground")
        for i in range(30):
            r = fb.add("Resistor", Resistance="10k"); rc = fb.add("Resistor", Resistance="100k")
            fb.w(prev, r + ".lPin"); fb.w(r + ".rPin", rc + ".lPin"); fb.w(rc + ".rPin", g + ".Gnd")
            prev = r + ".rPin"
fb.build = build

CASES = [(k, s) for k, s in (a.split(":") for a in sys.argv[1].split(","))]
KINDS = sys.argv[2].split(",")
for kind, span in CASES:
    t = {k: [] for k in KINDS}
    for rep in range(int(os.environ.get("REPS", "2"))):
        for f in (KINDS if rep == 0 else KINDS[::-1]):
            t[f].append(fb.bench(kind, f, span)[0])
    base = min(t["wire"])
    print("%-12s %-6s " % (kind, span) + "  ".join("%s %.3fs (%+.0f%%)" % (k, min(v), 100*(min(v)/base-1)) for k, v in t.items()), flush=True)
