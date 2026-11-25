"""⌘S in a part's properties dialog, and quitting with the caret in one of its fields.

Runs offscreen but not headless ($REGRESS_OFFSCREEN=1): a headless server closes every dialog as
it shows, and offscreen a new window is not activated, so `view props` activates the dialog.

- The file keys work wherever the keyboard focus is (user, 2026-11-25; Cmd+S here), in a
  window of the app's own too. A label typed in the dialog's Label field is only taken on
  Enter or when the field is left; Cmd+S takes it and saves the circuit, and the field keeps
  the caret.
- Quitting clears the circuit, which destroys its parts. With the caret in a field of an open
  properties dialog that crashed (found while checking the above): hiding the dialog took the
  focus from the field, which applied its text to its part after that part's properties were
  deleted (PropDialog::commitEdit, called on WindowDeactivate). The dialog is now detached from
  its part first (PropDialog::detach). Expected: the process exits with code 0.

Nothing is simulated: the expected results are file contents and the exit code.
"""

import os
os.environ["REGRESS_OFFSCREEN"] = "1"      # Before run_test() starts the instance

import re
import tempfile

from harness import run_test


def label_in(path):
    """The label of the first resistor as saved in the circuit file (None: the part is not in it)."""
    with open(path, encoding="utf-8") as f:
        for m in re.finditer(r"<item [^>]*>", f.read(), re.S):
            if 'CircId="Resistor-1"' in m.group(0):
                return re.search(r'label="([^"]*)"', m.group(0)).group(1)
    return None


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100")
    path = os.path.join(tempfile.mkdtemp(prefix="props-dialog-"), "props.sim1")
    sim.cli("circuit", "save", path)
    sim.cli("view", "props", "R1")                      # The dialog has the keyboard

    def typed(keys):
        """Replace the Label field's text (Cmd+A, then the keys); the caret stays in the field."""
        sim.cli("ui", "type", "cmd+a", "--window")
        sim.cli("ui", "type", *keys, "--window")

    def state():
        return "file: %s, modified: %s" % (label_in(path), sim.cli("circuit", "info")["modified"])

    sim.cli("ui", "type", "Tab", "--window")            # From the dialog's first control to its Label field
    typed(["R", "9"])
    got, modified = label_in(path), sim.cli("circuit", "info")["modified"]
    sim.check("a label typed in the dialog waits for Enter or for the field to be left",
              int(got == "R1" and not modified), 1, absolute=0, note=state())

    sim.cli("ui", "type", "cmd+s", "--window")
    got, modified = label_in(path), sim.cli("circuit", "info")["modified"]
    sim.check("Cmd+S with the caret in the dialog's Label field takes it and saves the circuit",
              int(got == "R9" and not modified), 1, absolute=0, note=state())

    typed(["R", "1", "0"])
    sim.cli("ui", "type", "cmd+s", "--window")
    sim.check("the field kept the caret: typed on, Cmd+S again", int(label_in(path) == "R10"), 1, absolute=0,
              note=state())

    sim.cli("quit")
    code = sim.proc.wait(timeout=60)
    sim.check("quits cleanly with the caret in a field of the open dialog", int(code == 0), 1, absolute=0,
              note="exit code %s" % code)


if __name__ == "__main__":
    run_test(test)
