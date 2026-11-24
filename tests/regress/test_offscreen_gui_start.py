"""The GUI build starts with QT_QPA_PLATFORM=offscreen (without --headless) instead of crashing.

At start the GUI writes the build time into the macOS title bar (uiMacTitleNote, uitheme_mac.mm),
taking window.winId() as an NSView. Under the offscreen platform the window id is not an NSView,
so sending it -window crashed the app (SIGSEGV in objc_msgSend, before the CLI answered). Only
--headless worked without a display, so the normal GUI path could not be tested without one.

Right result: under offscreen the title note is skipped (it belongs to the Cocoa platform only);
the app starts, answers the CLI, shows a circuit given on the command line and quits on request.
"""

import os
import subprocess
import tempfile
import time

from harness import run_test
from simctl import Sim, SimError

HERE = os.path.dirname(os.path.abspath(__file__))
CIRCUIT = os.path.join(HERE, "..", "circuits", "border_styles.sim1")   # Any small circuit: no reopen of the last one


def test(sim):
    socket = "simulide-offscreen-gui-%d" % os.getpid()
    env = dict(os.environ, QT_QPA_PLATFORM="offscreen")     # SIMULIDE_CONFIG_DIR: the harness's private copy
    log = os.path.join(tempfile.mkdtemp(prefix="offscreen-gui-"), "gui.log")
    with open(log, "w") as logf:
        proc = subprocess.Popen([sim.bin, "--socket", socket, os.path.abspath(CIRCUIT)], env=env,
                                stdout=logf, stderr=subprocess.STDOUT)
    gui = Sim(binary=sim.bin, socket=socket, verbose=False)
    answered = False
    try:
        for _ in range(150):                                    # Up to ~30 s for the start
            if proc.poll() is not None:
                break
            try:
                gui.cli("ping", timeout=5)
                answered = True
                break
            except (SimError, subprocess.TimeoutExpired):
                time.sleep(0.2)
        sim.check("GUI under offscreen: still running after start", int(proc.poll() is None), 1, absolute=0,
                  note="exit code %s, log %s" % (proc.poll(), log))
        sim.check("GUI under offscreen: answers the CLI", int(answered), 1, absolute=0)
        if answered:
            comps = gui.cli("comp", "list")
            sim.check("GUI under offscreen: the circuit given is open", int(len(comps) > 10), 1, absolute=0,
                      note="%d components" % len(comps))
            try:
                gui.cli("quit", "--discard", timeout=10)
            except (SimError, subprocess.TimeoutExpired):
                pass                                            # The reply may be lost as it quits
            try:
                proc.wait(timeout=20)
            except subprocess.TimeoutExpired:
                pass
            sim.check("GUI under offscreen: quits on request, exit 0", proc.poll(), 0, absolute=0)
    finally:
        if proc.poll() is None:
            proc.kill()
            proc.wait()


if __name__ == "__main__":
    run_test(test)
