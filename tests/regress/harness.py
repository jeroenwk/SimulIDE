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
"""

import glob
import os
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


class Headless:
    """A headless SimulIDE on a private socket, stopped on exit."""

    def __init__(self, binary):
        self.binary = binary
        self.socket = "simulide-regress-%d" % os.getpid()
        self.tmp = tempfile.mkdtemp(prefix="simulide-regress-")
        self.proc = None

    def __enter__(self):
        logf = open(os.path.join(self.tmp, "server.log"), "w")
        log("start %s --headless --socket %s (log %s/server.log)" % (self.binary, self.socket, self.tmp))
        self.proc = subprocess.Popen([self.binary, "--headless", "--socket", self.socket],
                                     stdout=logf, stderr=subprocess.STDOUT)
        sim = Sim(binary=self.binary, socket=self.socket, verbose=True)
        for _ in range(100):
            try:
                sim.cli("ping", timeout=5)
                log("server answers ping")
                return sim
            except (SimError, subprocess.TimeoutExpired):
                time.sleep(0.2)
        self.__exit__(None, None, None)
        raise RuntimeError("headless SimulIDE did not answer; see %s/server.log" % self.tmp)

    def __exit__(self, *exc):
        if self.proc and self.proc.poll() is None:
            log("stop server pid %d" % self.proc.pid)
            self.proc.terminate()
            try:
                self.proc.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.proc.kill()
        return False


def run_test(fn):
    """Run one test function against a fresh headless instance and exit with its result."""
    name = os.path.basename(sys.modules[fn.__module__].__file__ if fn.__module__ != "__main__" else sys.argv[0])
    binary = find_binary()
    if not binary or not os.access(binary, os.X_OK):
        log("simulide binary not found: build first or set SIMULIDE")
        sys.exit(2)
    log("test %s with %s" % (name, binary))
    try:
        with Headless(binary) as sim:
            fn(sim)
            passed = sim.report()
    except RuntimeError as e:
        log("harness error: %s" % e)
        sys.exit(2)
    except Exception:  # A crash or a SimError inside the test is a failure, not a harness error
        traceback.print_exc()
        passed = False
    log("%s %s" % ("PASS" if passed else "FAIL", name))
    sys.exit(0 if passed else 1)
