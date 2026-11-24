"""Harness self-check: a SimError raised inside a test is a failed test (exit 1), not a harness error.

simctl's SimError derives from RuntimeError, and run_test caught RuntimeError first, as a harness
error (exit 2, "the server did not start"). So a test whose CLI call was refused (or timed out:
SimHang is a SimError) was reported by run.sh as "(harness)", as if SimulIDE had not started,
instead of as a failure of the test.

Right result: a test that raises SimError exits 1 (failed); a server that never answers still
exits 2 (harness error), since that is what exit 2 means (tests/regress/README.md).
"""

import os
import subprocess
import sys
import tempfile

from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))

CHILD = """
import sys
sys.path.insert(0, %r)
from harness import run_test

def test(sim):
    sim.cli("no-such-command")       # Refused by the server: raises SimError

run_test(test)
"""


def run_child(env_extra):
    tmp = tempfile.mkdtemp(prefix="harness-simerror-")
    script = os.path.join(tmp, "child_test.py")
    with open(script, "w") as f:
        f.write(CHILD % HERE)
    env = dict(os.environ)
    env.pop("SIMULIDE_COPY", None)       # The child runs the same binary, no second copy
    env.update(env_extra)
    return subprocess.run([sys.executable, script], env=env, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, timeout=300).returncode


def test(sim):
    sim.check("a SimError inside a test exits 1 (failed)", run_child({"SIMULIDE": sim.bin}), 1, absolute=0)
    # /usr/bin/true is found and executable but never answers ping: the harness's own error
    sim.check("a server that never answers exits 2 (harness error)", run_child({"SIMULIDE": "/usr/bin/true"}), 2, absolute=0)


if __name__ == "__main__":
    run_test(test)
