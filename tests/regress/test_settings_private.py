"""The test servers never write the user's settings (TODO regress-settings).

Headless servers used the platform settings folder, the same simulide.ini as the user's GUI:
a test that switched currentFlow or partStatus left the user's GUI with them switched. The
harness now gives each test process a copy of the user's settings folder and SimulIDE reads
SIMULIDE_CONFIG_DIR instead (MainWindow::configDir).

- A setting changed through the CLI is written to the private copy.
- The user's simulide.ini is byte for byte the same as before the test.
"""
import os

from harness import run_test, USER_CONFIG


def user_ini():
    for d in USER_CONFIG:
        p = os.path.join(d, "simulide.ini")
        if os.path.isfile(p):
            return p
    return None


def test(sim):
    priv = os.path.join(os.environ["SIMULIDE_CONFIG_DIR"], "simulide.ini")
    real = user_ini()
    before = open(real, "rb").read() if real else None

    old = sim.cli("settings", "get", "currentMin")
    sim.cli("settings", "set", "currentMin", "123n")
    sim.cli("settings", "set", "currentFlow", "true")
    sim.cli("settings", "set", "currentFlow", "false")
    sim.cli("circuit", "new", "--discard")          # Settings are synced to disk on the way

    text = open(priv, encoding="utf-8", errors="replace").read() if os.path.isfile(priv) else ""
    sim.check("the change is in the private settings copy", 1 if "1.23e-07" in text or "123n" in text or "1.23e-7" in text else 0, 1,
              absolute=0, note=priv)
    after = open(real, "rb").read() if real else None
    sim.check("the user's simulide.ini is unchanged", 1 if before == after else 0, 1, absolute=0, note=str(real))
    sim.cli("settings", "set", "currentMin", str(old))


if __name__ == "__main__":
    run_test(test)
