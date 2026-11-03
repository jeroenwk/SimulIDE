"""simctl: drive a running SimulIDE through its CLI from Python, and check the result.

Stdlib only. Used by scenario scripts (e.g. led_driver/sim/stage_Ra_scenario.py):
open a circuit, flip switches, set the speed and the scope, run for a span of
simulated time, then read the scope as numbers, fit a time constant, capture
PNGs and check values against expectations.

    from simctl import Sim
    sim = Sim()                          # the visible GUI (default socket)
    sim.open("circuit.sim1", discard=True)
    sim.scope("SCOPE", timediv="20ms", voltdiv="5,0.2", trigger="none")
    sim.speed(1)                         # % of real time
    sim.run("200ms")
    t, v = sim.trace("SCOPE", 0, last="200ms")
    sim.check("VDD final", v[-1], 19.6, tol=0.02)
    sim.report()

Run from the Scripts pane, a script gets SIMULIDE (the binary) and SIMULIDE_SOCKET
(the window's socket) in its environment, and Sim() uses both. See docs/CLI.md for
the commands. Results tagged [SIMULATED] belong to the
model, not to the hardware.
"""

import json
import math
import os
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_BIN = os.path.join(HERE, "..", "build_XX", "executables", "SimulIDE_1.1.0-SR2",
                           "simulide.app", "Contents", "MacOS", "simulide")


class SimError(RuntimeError):
    pass


class Sim:
    def __init__(self, binary=None, socket=None, verbose=True):
        self.bin = os.path.abspath(binary or os.environ.get("SIMULIDE", DEFAULT_BIN))
        self.socket = socket or os.environ.get("SIMULIDE_SOCKET")   # Set when run from the Scripts pane
        self.verbose = verbose
        self.results = []          # (name, value, expected, ok, note)

    # --- raw CLI -------------------------------------------------------------
    def cli(self, *args, timeout=None):
        """Run one CLI command; return its result (JSON), raise SimError on failure."""
        cmd = [self.bin, "cli"]
        if self.socket:
            cmd += ["--socket", self.socket]
        cmd += ["--json"] + [str(a) for a in args]
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        try:
            r = json.loads(out.stdout)
        except json.JSONDecodeError:
            raise SimError("no JSON reply to %s: %s %s" % (" ".join(map(str, args)), out.stdout, out.stderr))
        if not r.get("ok"):
            raise SimError("%s: %s" % (" ".join(map(str, args)), r.get("error")))
        return r.get("result")

    def log(self, msg):
        if self.verbose:
            print(msg, flush=True)

    # --- circuit and simulation ---------------------------------------------
    def info(self):
        return self.cli("circuit", "info")

    def open(self, path, discard=False):
        args = ["circuit", "open", os.path.abspath(path)]
        if discard:
            args.append("--discard")
        self.log("open %s" % path)
        return self.cli(*args)

    def stop(self):
        return self.cli("sim", "stop")

    def start(self):
        return self.cli("sim", "start")

    def resume(self):
        return self.cli("sim", "resume")

    def speed(self, percent):
        self.log("speed %g %% of real time" % percent)
        return self.cli("sim", "speed", percent)

    def run(self, span):
        """Run for a span of simulated time (e.g. '200ms'), then pause. Waits without a
        timeout: a cancelled client would still leave the run queued in SimulIDE."""
        self.log("run %s" % span)
        return self.cli("sim", "run", "--for", span)

    def time(self):
        return float(self.cli("sim", "status")["time_ps"]) * 1e-12

    def setting(self, name, value):
        return self.cli("settings", "set", name, value)

    # --- parts -----------------------------------------------------------------
    def ui(self, comp, state):
        """Set a switch or toggle: state 'on' (pressed) or 'off'."""
        self.log("%s %s" % (comp, state))
        return self.cli("ui", "set", comp, state)

    def set(self, comp, **props):
        return self.cli("comp", "set", comp, *["%s=%s" % kv for kv in props.items()])

    def read(self, comp):
        return self.cli("read", comp)

    # --- scope -----------------------------------------------------------------
    def scope(self, comp, timediv=None, voltdiv=None, voltpos=None, trigger=None):
        args = ["scope", "set", comp]
        if timediv is not None: args += ["--timediv", timediv]
        if voltdiv is not None: args += ["--voltdiv", voltdiv]
        if voltpos is not None: args += ["--voltpos", voltpos]
        if trigger is not None: args += ["--trigger", trigger]
        self.log("scope %s %s" % (comp, " ".join(args[3:])))
        return self.cli(*args)

    def trace(self, comp, ch, last=None, max_samples=200000):
        """Scope samples of one channel: (times in s, values)."""
        args = ["scope", "data", comp, "--ch", ch, "--max", max_samples]
        if last:
            args += ["--last", last]
        r = self.cli(*args)
        s = r["channels"][0]["samples"]
        return [p[0] for p in s], [p[1] for p in s]

    def capture(self, comp=None, path=None, scale=3):
        if path is None:
            fd, path = tempfile.mkstemp(suffix=".png", prefix="simctl-")
            os.close(fd)
        args = ["capture", "circuit"] + ([comp] if comp else []) + ["--scale", scale, "--file", os.path.abspath(path)]
        return self.cli(*args)["file"]

    # --- analysis --------------------------------------------------------------
    @staticmethod
    def value_at(t, v, when):
        """Value of a trace at time `when` (the last sample at or before it)."""
        out = v[0]
        for ti, vi in zip(t, v):
            if ti > when:
                break
            out = vi
        return out

    @staticmethod
    def first_crossing(t, v, level, rising=True, after=0.0):
        """Time a trace first crosses `level` after `after` (linear interpolation)."""
        for i in range(1, len(t)):
            if t[i] < after:
                continue
            a, b = v[i-1], v[i]
            if (rising and a < level <= b) or (not rising and a > level >= b):
                if b == a:
                    return t[i]
                return t[i-1] + (level-a)/(b-a)*(t[i]-t[i-1])
        return None

    @staticmethod
    def tau_rise(t, v, t0, v0=None, vinf=None):
        """Time constant of a rise starting at t0: time to 63.2 % of (vinf - v0)."""
        if v0 is None:
            v0 = Sim.value_at(t, v, t0)
        if vinf is None:
            vinf = v[-1]
        tc = Sim.first_crossing(t, v, v0 + 0.632*(vinf-v0), rising=True, after=t0)
        return None if tc is None else tc - t0

    @staticmethod
    def tau_decay(t, v, t0, t1=None, vfloor=1e-3):
        """Time constant of an exponential decay towards 0 between t0 and t1:
        least-squares slope of ln(v) against t."""
        pts = [(ti, math.log(vi)) for ti, vi in zip(t, v)
               if ti >= t0 and (t1 is None or ti <= t1) and vi > vfloor]
        if len(pts) < 3:
            return None
        n = len(pts)
        mt = sum(p[0] for p in pts)/n
        ml = sum(p[1] for p in pts)/n
        num = sum((p[0]-mt)*(p[1]-ml) for p in pts)
        den = sum((p[0]-mt)**2 for p in pts)
        slope = num/den
        return None if slope >= 0 else -1/slope

    # --- checks ----------------------------------------------------------------
    def check(self, name, value, expected, tol=0.05, absolute=None, note=""):
        """Record a check: |value - expected| within tol (relative) or `absolute`."""
        if value is None:
            ok = False
        elif absolute is not None:
            ok = abs(value-expected) <= absolute
        else:
            ok = abs(value-expected) <= tol*abs(expected)
        self.results.append((name, value, expected, ok, note))
        self.log("  %-4s %s = %s (expected %s)%s" % ("ok" if ok else "FAIL", name, fmt(value), fmt(expected),
                                                  "  " + note if note else ""))
        return ok

    def report(self):
        passed = sum(1 for r in self.results if r[3])
        print("\n%-34s %12s %12s  %s" % ("check [SIMULATED]", "value", "expected", "result"))
        for name, value, expected, ok, note in self.results:
            print("%-34s %12s %12s  %s%s" % (name, fmt(value), fmt(expected), "ok" if ok else "FAIL",
                                            "  (" + note + ")" if note else ""))
        print("\npassed %d of %d" % (passed, len(self.results)))
        return passed == len(self.results)


def fmt(x):
    if x is None:
        return "-"
    if isinstance(x, float):
        a = abs(x)
        if a and (a < 1e-3 or a >= 1e4):
            return "%.3g" % x
        return "%.4g" % x
    return str(x)
