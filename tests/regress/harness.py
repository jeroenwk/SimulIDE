"""Regression test harness: each test gets its own headless SimulIDE on a private socket.

A test is a file tests/regress/test_<id>_<slug>.py with a function test(sim) that
uses tools/simctl.Sim (sim.cli, sim.run, sim.read, sim.check, ...) and returns nothing;
it fails when a sim.check fails or when it raises. Run one test directly
(python3 tests/regress/test_x.py) or all of them with tests/regress/run.sh.

    from harness import run_test
    def test(sim):
        sim.cli("circuit", "new", "--discard")
        ...
        sim.check("R1 current", sim.read("R1")["current"], 0.01, tol=0.01)
    if __name__ == "__main__":
        run_test(test)

The binary comes from $SIMULIDE, else the first build in build_XX/executables.
Exit code: 0 pass, 1 fail, 2 harness error (no binary, server did not start).
Debug lines are prefixed [regress].

Safety nets (also used by the sweeps in sweeps/):
- every CLI call has a timeout ($REGRESS_TIMEOUT, default 120 s wall time); a call that
  runs out raises SimHang, so a solver hang fails the test instead of blocking forever;
- sim.finite(name, comp) fails when a pin voltage of comp is None, NaN or beyond 1e6 V;
- $SIMULIDE_COPY=1 runs a private copy of the app, so a relink of build_XX (the next
  fix being built) does not break a long sweep;
- resources/data is synced into the built app before the server starts: data files are
  only copied at link time, so an XML-only fix would otherwise be invisible.
"""

import filecmp
import glob
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time
import traceback

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "tools"))
from simctl import Sim, SimError  # noqa: E402

PREFIX = "[regress]"
TIMEOUT = float(os.environ.get("REGRESS_TIMEOUT", "120"))


class SimHang(SimError):
    """A CLI call did not answer within the timeout: the server is probably stuck."""


class RegressSim(Sim):
    """simctl.Sim with a timeout on every call and a NaN/overflow check."""

    def cli(self, *args, timeout=None):
        try:
            return super().cli(*args, timeout=timeout or TIMEOUT)
        except subprocess.TimeoutExpired:
            log("HANG: no reply to '%s' within %.0f s" % (" ".join(map(str, args)), timeout or TIMEOUT))
            raise SimHang("no reply within %.0f s to: %s" % (timeout or TIMEOUT, " ".join(map(str, args))))

    def finite(self, name, comp, limit=1e6):
        """Check that every pin voltage of comp is a finite number below limit volts."""
        pins = (self.read(comp) or {}).get("pins") or {}
        bad = {k: v for k, v in pins.items()
               if not isinstance(v, (int, float)) or math.isnan(v) or math.isinf(v) or abs(v) > limit}
        if bad:
            self.log("  not finite: %s %s" % (comp, bad))
        return self.check(name, 0 if bad else 1, 1, absolute=0.5, note="pins %s" % (bad or "finite"))


def log(msg):
    print("%s %s" % (PREFIX, msg), flush=True)


def find_binary():
    b = os.environ.get("SIMULIDE")
    if b:
        return b
    hits = sorted(glob.glob(os.path.join(ROOT, "build_XX", "executables", "*", "simulide.app",
                                          "Contents", "MacOS", "simulide")))
    hits += sorted(glob.glob(os.path.join(ROOT, "build_XX", "executables", "*", "simulide")))
    return hits[0] if hits else None


def sync_data(binary):
    """Copy resources/data files that differ into the app's data folder (normally done only at link)."""
    src = os.path.join(ROOT, "resources", "data")
    dst = os.path.join(os.path.dirname(binary), "data")
    if not os.path.isdir(src) or not os.path.isdir(dst):
        return 0
    n = 0
    for d, _, files in os.walk(src):
        for f in files:
            a = os.path.join(d, f)
            b = os.path.join(dst, os.path.relpath(a, src))
            if not os.path.exists(b) or not filecmp.cmp(a, b, shallow=False):
                os.makedirs(os.path.dirname(b), exist_ok=True)
                shutil.copy2(a, b)
                n += 1
                log("data sync: %s" % os.path.relpath(a, ROOT))
    return n


def private_copy(binary):
    """Copy the app (macOS bundle or executables folder) to a temp folder and return the copy's binary."""
    app = binary
    while app != "/" and not app.endswith(".app"):
        app = os.path.dirname(app)
    base = app if app.endswith(".app") else os.path.dirname(binary)
    dst = os.path.join(tempfile.mkdtemp(prefix="simulide-copy-"), os.path.basename(base))
    shutil.copytree(base, dst, symlinks=True)
    copy = os.path.join(dst, os.path.relpath(binary, base))
    log("running a private copy of the app: %s" % copy)
    return copy


class Headless:
    """A headless SimulIDE on a private socket, stopped on exit."""

    def __init__(self, binary):
        sync_data(binary)
        self.copy_dir = None
        if os.environ.get("SIMULIDE_COPY") == "1":
            binary = private_copy(binary)
            self.copy_dir = binary.split("/simulide.app/")[0] if "/simulide.app/" in binary else os.path.dirname(binary)
        self.binary = binary
        self.socket = "simulide-regress-%d" % os.getpid()
        self.tmp = tempfile.mkdtemp(prefix="simulide-regress-")
        self.proc = None

    def __enter__(self):
        logf = open(os.path.join(self.tmp, "server.log"), "w")
        log("start %s --headless --socket %s (log %s/server.log)" % (self.binary, self.socket, self.tmp))
        self.proc = subprocess.Popen([self.binary, "--headless", "--socket", self.socket],
                                     stdout=logf, stderr=subprocess.STDOUT)
        sim = RegressSim(binary=self.binary, socket=self.socket, verbose=True)
        for _ in range(100):
            try:
                sim.cli("ping", timeout=5)
                log("server answers ping")
                return sim
            except (SimError, subprocess.TimeoutExpired):
                time.sleep(0.2)
        self.__exit__(None, None, None)
        raise RuntimeError("headless SimulIDE did not answer; see %s/server.log" % self.tmp)

    def restart(self):
        """Stop the server (killing it when stuck) and start a new one; returns the new sim."""
        log("restart server")
        self.__exit__(None, None, None)
        return self.__enter__()

    def __exit__(self, *exc):
        if self.proc and self.proc.poll() is None:
            log("stop server pid %d" % self.proc.pid)
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        return False


def cleanup(h):
    """Remove the private app copy of a Headless (after __exit__)."""
    if h.copy_dir and os.path.basename(h.copy_dir).startswith("simulide-copy-"):
        shutil.rmtree(h.copy_dir, ignore_errors=True)
        log("removed the private copy %s" % h.copy_dir)


def run_test(fn):
    """Run one test function against a fresh headless instance and exit with its result."""
    name = os.path.basename(sys.modules[fn.__module__].__file__ if fn.__module__ != "__main__" else sys.argv[0])
    binary = find_binary()
    if not binary or not os.access(binary, os.X_OK):
        log("simulide binary not found: build first or set SIMULIDE")
        sys.exit(2)
    log("test %s with %s" % (name, binary))
    h = Headless(binary)
    try:
        with h as sim:
            fn(sim)
            passed = sim.report()
    except RuntimeError as e:
        log("harness error: %s" % e)
        sys.exit(2)
    except Exception:  # A crash or a SimError inside the test is a failure, not a harness error
        traceback.print_exc()
        passed = False
    cleanup(h)
    log("%s %s" % ("PASS" if passed else "FAIL", name))
    sys.exit(0 if passed else 1)
