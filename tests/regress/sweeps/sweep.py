#!/usr/bin/env python3
"""Sweeps over the whole component library (or the examples) that find classes of bugs at once.

Each sweep runs a private copy of the app on a headless server, survives crashes and hangs
(the server is restarted and the finding recorded) and prints one FIND line per finding:

    FIND <sweep> <part> <property> <value> <CRASH|HANG|DIFF|SET|NAN|ERROR> <detail>

Sweeps
  roundtrip  set every numeric property far from its default (x --factor, default 10; ints +1,
             bools flipped), save, reopen, compare: setters that depend on load order or on
             another property (Min/Max pairs) show up. --order rev sets them in reverse order.
             Found BH-001..003, BH-017..019.
  strings    malformed text in every string/text property (spaces, separators, XML characters,
             unicode, empty, very long), then save and reopen. Found BH-020, BH-012.
  garble     every attribute of a saved part replaced by junk numbers/booleans, then open:
             crash-on-load bugs. Found BH-022..024.
  extremes   every numeric property at 0 and -1 on a part whose pins are driven through 1 kOhm,
             run 2 ms: NaN or huge voltages, hangs, crashes. Found BH-025..031.
  examples   open and run every circuit in resources/examples for 20 ms: load errors, crashes,
             hangs, solver faults.

Usage (from the SimulIDE folder; the build in build_XX, or $SIMULIDE):
  python3 tests/regress/sweeps/sweep.py roundtrip [--factor 0.1] [--order rev]
  python3 tests/regress/sweeps/sweep.py extremes --parts 'Mosfet|BJT' --limit 20
  python3 tests/regress/sweeps/sweep.py examples --start-at Arduino
Options: --parts REGEX (part names or example paths), --start-at NAME, --limit N, --out FILE.
Exit code: 0 nothing found, 1 findings, 2 harness error. Debug lines are prefixed [sweep].
"""

import argparse
import glob
import math
import os
import re
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))
os.environ.setdefault("SIMULIDE_COPY", "1")   # a relink of build_XX must not break a long sweep
from harness import Headless, SimHang, SimError, cleanup, find_binary, ROOT  # noqa: E402

TMP = tempfile.mkdtemp(prefix="sweep-")
FINDINGS = []
OUT = None
SKIP_PARTS = ("Z80",)          # very slow to create; covered by BH-012's own test
NOT_PROPS = ("itemtype", "CircType", "Id", "uid", "objectName", "label", "Links", "Mainmcu")
STRINGS = ["", "abc", "a b", "x,y;z", "a<b>&\"q'", "ünï €", "12", "-1", "0x", "a" * 300]
GARBLE = ["2", "3", "7", "100", "-100", "1000000", "0.5", "-0.5", "true", "false", "", "x"]


def log(msg):
    print("[sweep] %s" % msg, flush=True)


def find(sweep, part, prop, value, kind, detail=""):
    line = "FIND %s %s %s %r %s %s" % (sweep, part, prop, value, kind, str(detail)[:200])
    FINDINGS.append(line)
    print(line, flush=True)
    if OUT:
        OUT.write(line + "\n")
        OUT.flush()


class Server:
    """A headless server that is restarted after a crash or a hang."""

    def __init__(self):
        self.h = Headless(find_binary())
        self.sim = self.h.__enter__()

    def alive(self):
        return self.h.proc is not None and self.h.proc.poll() is None

    def restart(self):
        self.sim = self.h.restart()

    def close(self):
        self.h.__exit__(None, None, None)
        cleanup(self.h)


def guarded(srv, sweep, part, prop, value, fn):
    """Run fn(sim); turn a crash or a hang into a finding and restart the server. True when fn ran."""
    try:
        fn(srv.sim)
        return True
    except SimHang as e:
        find(sweep, part, prop, value, "HANG", e)
        srv.restart()
    except SimError as e:
        if not srv.alive():
            find(sweep, part, prop, value, "CRASH", e)
            srv.restart()
        elif sweep != "garble":
            find(sweep, part, prop, value, "ERROR", e)
    return False


def parts(srv, args):
    lib = srv.sim.cli("lib", "list")
    names = [x["name"] for x in lib if x.get("category") not in ("Micro", "MCUs")
             and not str(x.get("category", "")).startswith("Boards") and x["name"] not in SKIP_PARTS]
    if args.parts:
        names = [n for n in names if re.search(args.parts, n)]
    if args.start_at and args.start_at in names:
        names = names[names.index(args.start_at):]
    return names[:args.limit] if args.limit else names


def new_part(sim, name):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", name, "--label", "X1", "--at", "0,0")
    return sim.cli("comp", "get", "X1")


def props_of(g):
    return {p["name"]: p["value"] for p in g.get("properties", [])}


def roundtrip_file(sim, name):
    path = os.path.join(TMP, "rt.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("circuit", "open", path, "--discard")
    return props_of(sim.cli("comp", "get", "X1"))


# --- sweeps --------------------------------------------------------------------------------------
def sweep_roundtrip(srv, args):
    for n in parts(srv, args):
        log("part %s" % n)
        state = {}

        def run(sim):
            g = new_part(sim, n)
            plist = g.get("properties", [])
            for p in (plist[::-1] if args.order == "rev" else plist):
                if p["name"] in NOT_PROPS:
                    continue
                m = re.match(r"^(-?[\d.eE+-]+)\s*(.*)$", str(p["value"]))
                if p["type"] == "double" and m:
                    v = float(m.group(1))
                    nv = round(v * args.factor, 6) if v else 0.37
                    val = "%s%s" % (nv, m.group(2).replace(" ", ""))
                elif p["type"] in ("int", "uint") and m:
                    val = str(int(float(m.group(1))) + 1)
                elif p["type"] == "bool":
                    val = "false" if p["value"] == "true" else "true"
                else:
                    continue
                try:
                    sim.cli("comp", "set", "X1", "%s=%s" % (p["name"], val))
                except SimHang:
                    raise
                except SimError:
                    if not srv.alive():
                        raise
            state["a"] = props_of(sim.cli("comp", "get", "X1"))
            state["b"] = roundtrip_file(sim, n)
        if guarded(srv, "roundtrip", n, "-", "x%s" % args.factor, run):
            a, b = state["a"], state["b"]
            for k in a:
                if k not in NOT_PROPS and a[k] != b.get(k, "MISSING"):
                    find("roundtrip", n, k, a[k], "DIFF", "reloads as %r" % b.get(k, "MISSING"))


def sweep_strings(srv, args):
    for n in parts(srv, args):
        log("part %s" % n)
        g = {}
        if not guarded(srv, "strings", n, "-", "-", lambda sim: g.update(new_part(sim, n))):
            continue
        for p in g.get("properties", []):
            if p["type"] not in ("string", "textEdit") or p["name"] in NOT_PROPS:
                continue
            for v in STRINGS:
                state = {}

                def run(sim):
                    new_part(sim, n)
                    sim.cli("comp", "set", "X1", "%s=%s" % (p["name"], v))
                    state["a"] = props_of(sim.cli("comp", "get", "X1"))
                    state["b"] = roundtrip_file(sim, n)
                if guarded(srv, "strings", n, p["name"], v, run):
                    a, b = state["a"], state["b"]
                    for k in a:
                        if k not in NOT_PROPS and a[k] != b.get(k, "MISSING"):
                            find("strings", n, k, v, "DIFF", "%r reloads as %r" % (a[k], b.get(k, "MISSING")))


def sweep_garble(srv, args):
    base = os.path.join(TMP, "g0.sim1")
    bad = os.path.join(TMP, "g1.sim1")
    for n in parts(srv, args):
        log("part %s" % n)

        def save(sim):
            new_part(sim, n)
            sim.cli("circuit", "save", base)
        if not guarded(srv, "garble", n, "-", "-", save):
            continue
        txt = open(base).read()
        m = re.search(r"<item [^>]*?>", txt, re.S)
        if not m:
            continue
        item = m.group(0)
        attrs = [a for a in re.findall(r'\b([A-Za-z_][\w]*)="[^"]*"', item) if a not in NOT_PROPS]
        for a in attrs:
            for v in GARBLE:
                new = re.sub(r'\b%s="[^"]*"' % re.escape(a), '%s="%s"' % (a, v), item, count=1)
                with open(bad, "w") as f:
                    f.write(txt.replace(item, new))

                def run(sim):
                    sim.cli("circuit", "open", bad, "--discard")
                    sim.cli("sim", "run", "--for", "1ms")
                guarded(srv, "garble", n, a, v, run)


def sweep_extremes(srv, args):
    volts = [5, 2, 0.7, 3.3, 1, 4]

    def build(sim, n, prop=None, val=None):
        sim.cli("circuit", "new", "--discard")
        sim.cli("comp", "add", n, "--label", "D", "--at", "0,0")
        if prop:
            try:
                sim.cli("comp", "set", "D", "%s=%s" % (prop, val))
            except SimHang:
                raise
            except SimError:
                if not srv.alive():
                    raise
        g = sim.cli("comp", "get", "D")
        sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-300,-200")
        for i, p in enumerate(g.get("pins", [])):
            sim.cli("comp", "add", "Fixed Voltage", "--label", "V%d" % i, "--at", "-300,%d" % (i * 100),
                    "Voltage=%s" % volts[i % 6], "Out=true")
            sim.cli("comp", "add", "Resistor", "--label", "R%d" % i, "--at", "-150,%d" % (i * 100), "Resistance=1k")
            sim.cli("wire", "add", "V%d.outnod" % i, "R%d.lPin" % i)
            try:
                sim.cli("wire", "add", "R%d.rPin" % i, p["ref"])
            except SimError as e:
                if "hidden" not in str(e):
                    raise
        return g

    for n in parts(srv, args):
        log("part %s" % n)
        g = {}
        if not guarded(srv, "extremes", n, "-", "-", lambda sim: g.update(build(sim, n))):
            continue
        for p in [p for p in g.get("properties", []) if p["type"] in ("double", "int", "uint")]:
            if p["name"] in NOT_PROPS:
                continue
            for v in ("0", "-1"):
                state = {}

                def run(sim):
                    build(sim, n, p["name"], v)
                    sim.cli("sim", "run", "--for", "2ms")
                    state["pins"] = (sim.read("D") or {}).get("pins") or {}
                if guarded(srv, "extremes", n, p["name"], v, run):
                    bad = {k: x for k, x in state["pins"].items()
                           if x is None or (isinstance(x, float) and (math.isnan(x) or math.isinf(x) or abs(x) > 1e3))}
                    if bad:
                        find("extremes", n, p["name"], v, "NAN", bad)


def sweep_examples(srv, args):
    files = sorted(glob.glob(os.path.join(ROOT, "resources", "examples", "**", "*.sim1"), recursive=True))
    if args.parts:
        files = [f for f in files if re.search(args.parts, f)]
    if args.start_at:
        files = [f for f in files if f >= os.path.join(ROOT, "resources", "examples", args.start_at)] or files
    files = files[:args.limit] if args.limit else files
    for f in files:
        rel = os.path.relpath(f, ROOT)
        log("example %s" % rel)
        state = {}

        def run(sim):
            sim.cli("circuit", "open", f, "--discard")
            # An example saved at a very slow speed (e.g. stepsPS=100 = 0.0001 % of
            # real time) would need minutes of wall time for 20 ms and look like a
            # hang: run every example at 100 % (IMP-029).
            sim.cli("settings", "set", "speed=100")
            sim.cli("sim", "run", "--for", "20ms")
            state["log"] = sim.cli("log", "--kind", "solver")
        if guarded(srv, "examples", rel, "-", "-", run):
            lg = state.get("log")
            ev = lg.get("events") if isinstance(lg, dict) else lg
            if ev:
                find("examples", rel, "-", "-", "SOLVER", ev[:3])


SWEEPS = {"roundtrip": sweep_roundtrip, "strings": sweep_strings, "garble": sweep_garble,
          "extremes": sweep_extremes, "examples": sweep_examples}


def main():
    global OUT
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("sweep", choices=sorted(SWEEPS))
    ap.add_argument("--parts", help="regex on part names (or example paths)")
    ap.add_argument("--start-at", help="first part (or example folder) to sweep")
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--factor", type=float, default=10.0, help="roundtrip: multiply doubles by this")
    ap.add_argument("--order", choices=("fwd", "rev"), default="fwd", help="roundtrip: property order")
    ap.add_argument("--out", help="also append the FIND lines to this file")
    args = ap.parse_args()
    if not find_binary():
        log("simulide binary not found: build first or set SIMULIDE")
        sys.exit(2)
    OUT = open(args.out, "a") if args.out else None
    t0 = time.time()
    log("sweep %s (tmp %s)" % (args.sweep, TMP))
    try:
        srv = Server()
    except RuntimeError as e:
        log("harness error: %s" % e)
        sys.exit(2)
    try:
        SWEEPS[args.sweep](srv, args)
    finally:
        srv.close()
    log("done in %.0f s: %d finding(s)" % (time.time() - t0, len(FINDINGS)))
    sys.exit(1 if FINDINGS else 0)


if __name__ == "__main__":
    main()
