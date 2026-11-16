"""A headless server quits after a time without CLI requests (--idle-exit; --keep stays).

Headless servers started for a session or a script never exited: 44 of them, days old,
held 1.9 GB. A headless SimulIDE now quits after 30 min without a request (--idle-exit
TIME changes it, --keep or --idle-exit 0 keeps it), counting from the last request or
reply, and never while a request is still running.
"""

import os
import subprocess
import time

from harness import run_test, RegressSim
from simctl import SimError


def start(binary, sock, *extra):
    p = subprocess.Popen([binary, "--headless", "--socket", sock, *extra],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    s = RegressSim(binary=binary, socket=sock, verbose=False)
    for _ in range(100):
        try:
            s.cli("ping", timeout=5)
            return p, s
        except (SimError, subprocess.TimeoutExpired):
            time.sleep(0.2)
    p.kill()
    raise RuntimeError("server on %s did not answer" % sock)


def gone_after(p, secs):
    try:
        p.wait(timeout=secs)
        return True
    except subprocess.TimeoutExpired:
        return False


def test(sim):
    pid = os.getpid()
    p, s = start(sim.bin, "regress-idle-%d" % pid, "--idle-exit", "3s")
    try:
        for _ in range(3):                    # requests 2 s apart keep it alive past 3 s
            time.sleep(2)
            s.cli("ping", timeout=5)
        sim.check("alive while requests come (6 s, 3 s idle limit)", int(p.poll() is None), 1, absolute=0)
        s.cli("circuit", "new", "--discard")
        s.cli("sim", "run", "--for", "1ms", timeout=60)
        sim.check("exits 3 s after the last request", int(gone_after(p, 10)), 1, absolute=0)
        sim.check("exit code 0", p.returncode if p.returncode is not None else -1, 0, absolute=0)
    finally:
        if p.poll() is None:
            p.kill()

    p, s = start(sim.bin, "regress-keep-%d" % pid, "--idle-exit", "1s", "--keep")
    try:
        sim.check("--keep: still running 4 s later", int(not gone_after(p, 4)), 1, absolute=0)
    finally:
        p.terminate()
        gone_after(p, 10)


run_test(test)
