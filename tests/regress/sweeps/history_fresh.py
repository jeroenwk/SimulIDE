#!/usr/bin/env python3
"""Sweep: saved key frames over every circuit of the repos (specs/sim-control.md §5.1).

For each circuit: open, run --warm (5 ms, fast), `sim history check --fresh`: the state is saved, the
run goes to 3 checkpoints over --span, then the simulation is stopped and started again, the
saved state loaded and the same checkpoints run: every node voltage and every element's state
must be the same. FIND lines name the circuits that differ (and the elements), or fail.

    tests/regress/sweeps/history_fresh.py [--limit N] [--match TEXT] [--warm 5ms] [--span 1ms] [--out FILE]

Circuits: ~/perso/electronics books/, youtube/, led_driver/, slidymoove/, SimulIDE samples/,
resources/examples/, tests/circuits/. A crash or a hang restarts the server and goes on.
"""
import argparse
import glob
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, ".."))
import harness  # noqa: E402

ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))       # SimulIDE/
ELEC = os.path.dirname(ROOT)


def circuits():
    pats = [ELEC + "/books/**/*.sim1", ELEC + "/youtube/**/*.sim1", ELEC + "/led_driver/**/*.sim1",
            ELEC + "/slidymoove/**/*.sim1", ROOT + "/samples/**/*.sim1", ROOT + "/resources/examples/**/*.sim1",
            ROOT + "/tests/circuits/**/*.sim1"]
    out = []
    for p in pats:
        out += sorted(glob.glob(p, recursive=True))
    return [c for c in out if "/build_" not in c and ".keyframes/" not in c]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--match", default="")
    ap.add_argument("--span", default="1ms")
    ap.add_argument("--warm", default="5ms", help="run this long before the check (firmware set-up)")
    ap.add_argument("--out", default="")
    ap.add_argument("--dump", action="store_true", help="--xproc: the bytes of the first element that differs")
    ap.add_argument("--only", default="", help="a JSON result of an earlier run: only its differ/error circuits")
    ap.add_argument("--reopen", action="store_true", help="load the circuit file again before the second pass")
    ap.add_argument("--xproc", action="store_true", help="instead: run each circuit in two processes and compare "
                    "the full saved state (finds pointers and hash orders in keyframes)")
    a = ap.parse_args()
    files = [c for c in circuits() if a.match in c]
    if a.only:
        prev = json.load(open(a.only))
        keep = set(prev.get("differ", {})) | set(prev.get("error", {}))
        files = [c for c in files if os.path.relpath(c, ELEC) in keep]
    if a.limit:
        files = files[:a.limit]
    if a.xproc:
        return xproc(files, a)
    h = harness.Headless(harness.find_binary())
    sim = h.__enter__()
    res = {"ok": [], "differ": {}, "skip": {}, "error": {}}
    t0 = time.time()
    for i, f in enumerate(files):
        rel = os.path.relpath(f, ELEC)
        try:
            c = sim.cli
            c("circuit", "open", f, "--discard", timeout=60)
            c("settings", "set", "history", "true")
            c("sim", "speed", "1000000")
            hist = None
            c("sim", "run", "--for", a.warm, timeout=120)
            hist = c("sim", "history")
            if hist.get("missing"):
                res["skip"][rel] = "missing: " + ", ".join(hist["missing"])
                c("sim", "stop")
                continue
            r = c("sim", "history", "check", "--reopen" if a.reopen else "--fresh", "--span", a.span, "--points", "3", timeout=180)
            if r["ok"]:
                res["ok"].append(rel)
            else:
                res["differ"][rel] = {"same": r["same"], "same_nodes": r["same_nodes"], "differ": r["differ"][:12]}
                print("FIND %s: %d/3 same (%d nodes) differ %s" % (rel, r["same"], r["same_nodes"], r["differ"][:6]), flush=True)
            c("sim", "stop")
        except Exception as e:  # A hang or a crash: note it, restart the server
            msg = str(e).splitlines()[0][:200] if str(e) else type(e).__name__
            res["error"][rel] = msg
            print("ERROR %s: %s" % (rel, msg), flush=True)
            try:
                sim = h.restart()
            except Exception:
                sim = h.__enter__()
        if (i+1) % 25 == 0:
            print("[%d/%d] %.0f s: ok %d, differ %d, skip %d, error %d" % (i+1, len(files), time.time()-t0, len(res["ok"]),
                  len(res["differ"]), len(res["skip"]), len(res["error"])), flush=True)
    h.__exit__(None, None, None)
    print("done %d circuits in %.0f s: ok %d, differ %d, skip %d, error %d" % (len(files), time.time()-t0, len(res["ok"]),
          len(res["differ"]), len(res["skip"]), len(res["error"])))
    if a.out:
        with open(a.out, "w") as fo:
            json.dump(res, fo, indent=1)


def xproc(files, a):
    """Two servers side by side: the same circuit, the same run, the full saved state compared."""
    hs = [harness.Headless(harness.find_binary()) for _ in range(2)]
    for k, h in enumerate(hs):
        h.socket += "-%d" % k
    sims = [h.__enter__() for h in hs]
    res = {"ok": [], "differ": {}, "error": {}}
    t0 = time.time()
    for i, f in enumerate(files):
        rel = os.path.relpath(f, ELEC)
        try:
            out = []
            for sim in sims:
                c = sim.cli
                c("circuit", "open", f, "--discard", timeout=60)
                c("sim", "speed", "1000000")
                c("settings", "set", "runSeed", "12345")    # Flip-flops and noise are random on purpose:
                c("settings", "set", "noiseSeed", "4242")   # the same seeds (on the circuit's Simulator)
                c("sim", "run", "--for", a.warm, timeout=120)
                out.append(c("sim", "hash", "--elements"))
            x, y = out
            if x.get("state") != y.get("state") and a.dump:
                for e, g in zip(x.get("elements", []), y.get("elements", [])):
                    if e == g:
                        continue
                    eid = e.split(":")[0].split(" ", 1)[1]
                    da, db = [bytes.fromhex(sm.cli("sim", "hash", "--dump", eid).get("dump", "")) for sm in sims]
                    off = [j for j in range(min(len(da), len(db))) if da[j] != db[j]]
                    print("  DUMP %s (%d B) differs at %s" % (e.split(":")[0], len(da), off[:16]), flush=True)
                    if off:
                        o = max(0, off[0]-4)
                        print("   a %s\n   b %s" % (da[o:o+32].hex(), db[o:o+32].hex()), flush=True)
                    break
            for sm in sims:
                sm.cli("sim", "stop")
            if x.get("state") == y.get("state"):
                res["ok"].append(rel)
            else:
                els = [e.split(":")[0] for e, g in zip(x.get("elements", []), y.get("elements", [])) if e != g]
                nodes = sum(1 for e, g in zip(x.get("nodes", []), y.get("nodes", [])) if e != g)
                res["differ"][rel] = {"run_same": x["hash"] == y["hash"], "elements": els[:12], "nodes": nodes,
                                      "events": x.get("events") == y.get("events")}
                print("FIND %s: run %s, state differs in %s (nodes %d)" % (rel, "same" if x["hash"] == y["hash"] else "DIFFERS",
                      els[:6], nodes), flush=True)
        except Exception as e:
            msg = str(e).splitlines()[0][:200] if str(e) else type(e).__name__
            res["error"][rel] = msg
            print("ERROR %s: %s" % (rel, msg), flush=True)
            sims = [h.restart() for h in hs]
        if (i+1) % 25 == 0:
            print("[%d/%d] %.0f s: ok %d, differ %d, error %d" % (i+1, len(files), time.time()-t0, len(res["ok"]),
                  len(res["differ"]), len(res["error"])), flush=True)
    for h in hs:
        h.__exit__(None, None, None)
    print("done %d circuits in %.0f s: ok %d, differ %d, error %d" % (len(files), time.time()-t0, len(res["ok"]),
          len(res["differ"]), len(res["error"])))
    if a.out:
        with open(a.out, "w") as fo:
            json.dump(res, fo, indent=1)


if __name__ == "__main__":
    main()
