"""The file keys (Cmd+N, Cmd+O, Cmd+S, Shift+Cmd+S) work wherever the keyboard focus is (user, 2026-11-25).

Before: they were read by the canvas only (Circuit::keyPressEvent). After a click on Messages or
the toolbar, a speed typed in the toolbar box, the start of a simulation (the canvas gives up the
focus then) or in the Description tab, they did nothing until the canvas was clicked first.

Now CircuitWidget::fileKey, in the key filter that reads the simulation keys, acts from any widget
of the main window and from the other windows of the app. As before: on the canvas the keys are the
canvas's own (Circuit::keyPressEvent); in the code editor they act on the file being edited, not on
the circuit. A save takes a description still being typed in the Description tab as it stands.

Nothing is simulated: the expected results are file contents, and the dialogs that a headless
server reports as it closes them (Open and Save As open a file dialog, New with unsaved changes
asks whether to save them).
"""

import json
import os
import re
import subprocess
import tempfile

from harness import run_test


def r1_resistance(path):
    """R1's resistance as saved in the circuit file (None: no R1 in it)."""
    with open(path, encoding="utf-8") as f:
        for m in re.finditer(r"<item [^>]*>", f.read(), re.S):
            if 'label="R1"' in m.group(0):
                r = re.search(r'Resistance="([^"]*)"', m.group(0))
                return r.group(1) if r else None
    return None


def raw_reply(sim, *args):
    """The whole JSON reply of one CLI call (sim.cli returns its result only)."""
    cmd = [sim.bin, "cli", "--socket", sim.socket, "--json"] + list(args)
    return json.loads(subprocess.run(cmd, capture_output=True, text=True, timeout=60).stdout)


def closed_dialog(reply):
    """The dialog a headless server closed during a call, as it reported it ('' when none)."""
    return next((m for m in reply.get("messages", []) if m.startswith("dialog closed (headless)")), "")


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,100", "Resistance=1000")
    tmp = tempfile.mkdtemp(prefix="file-keys-")
    path = os.path.join(tmp, "keys.sim1")
    sim.cli("circuit", "save", path)
    sim.check("saved through the CLI: R1 is 1000 Ω", int(r1_resistance(path) == "1000 Ω"), 1, absolute=0,
              note=str(r1_resistance(path)))

    def edit():
        """Give R1 a resistance it did not have yet; return how it is written in the file."""
        value = 1000*(2 + len(sim.results))
        sim.cli("comp", "set", "R1", "Resistance=%d" % value)
        return "%d Ω" % value

    def saves(name, focus, key="cmd+s", window=True):
        """Focus as `focus` says, type the key: the file must hold the change and the circuit be unmodified."""
        want = edit()
        focus()
        # --window: the keys go to the focused widget (the canvas itself gets them otherwise)
        args = ["ui", "type", key] + (["--window"] if window else [])
        sim.cli(*args)
        got, modified = r1_resistance(path), sim.cli("circuit", "info")["modified"]
        sim.check(name, int(got == want and not modified), 1, absolute=0,
                  note="file: %s (wanted %s), modified: %s" % (got, want, modified))

    def not_saved(name, before):
        got, modified = r1_resistance(path), sim.cli("circuit", "info")["modified"]
        sim.check(name, int(got == before and modified), 1, absolute=0,
                  note="file: %s, modified: %s" % (got, modified))

    def type_keys(*args):
        """Type keys where the focus is; return the whole reply (the dialogs closed)."""
        return raw_reply(sim, "ui", "type", *args, "--window")

    saves("canvas focused: Cmd+S saves (as before)", lambda: sim.cli("ui", "focus", "canvas"), window=False)
    saves("no widget focused (as after a start): Cmd+S saves", lambda: sim.cli("ui", "focus", "none"))
    saves("Messages focused: Cmd+S saves", lambda: sim.cli("ui", "click", "--widget", "messages"))
    saves("toolbar clicked: Cmd+S saves", lambda: sim.cli("ui", "click", "--widget", "toolbar"))
    saves("simulation running: Cmd+S saves", lambda: sim.cli("sim", "start"))
    sim.cli("sim", "stop")

    # Not the save key: other modifiers, or none (meta is the Control key on macOS)
    before = r1_resistance(path)
    edit()
    sim.cli("ui", "focus", "none")
    for k in ("cmd+alt+s", "cmd+meta+s", "alt+s", "shift+s", "s"):
        sim.cli("ui", "type", k, "--window")
    not_saved("Cmd+Alt+S, Cmd+Ctrl+S, Alt+S, Shift+S and S alone do not save", before)

    # Save As: its file dialog, which a headless server closes as it shows
    reply = type_keys("cmd+shift+s")
    sim.check("Shift+Cmd+S with no widget focused opens the Save Circuit dialog",
              int(closed_dialog(reply).endswith("[Save Circuit]")), 1, absolute=0, note=closed_dialog(reply))
    not_saved("... which saves nothing", before)
    sim.cli("circuit", "save", path)

    # New and Open (user, 2026-11-25). Open opens nothing here: its file dialog is closed at once.
    # New asks first when there are unsaved changes; closed, that prompt counts as neither Save nor
    # Cancel, so the new circuit is made anyway (the same as from the toolbar)
    sim.cli("ui", "focus", "none")
    reply = type_keys("cmd+o")
    sim.check("Cmd+O with no widget focused opens the Load Circuit dialog",
              int(closed_dialog(reply).endswith("[Load Circuit]")), 1, absolute=0, note=closed_dialog(reply))
    info = sim.cli("circuit", "info")
    sim.check("... which opens nothing: the circuit is still the one saved",
              int(os.path.realpath(info["file"]) == os.path.realpath(path) and not info["modified"]), 1, absolute=0,
              note=json.dumps(info)[:200])

    edit()                                                  # Unsaved changes
    reply = type_keys("cmd+n")
    sim.check("Cmd+N with unsaved changes asks whether to save them",
              int("Circuit has been modified" in closed_dialog(reply)), 1, absolute=0, note=closed_dialog(reply))
    sim.cli("circuit", "open", path, "--discard")           # Whatever that left: back to the saved circuit

    sim.cli("ui", "click", "--widget", "toolbar")
    sim.cli("ui", "type", "cmd+n", "--window")
    info = sim.cli("circuit", "info")
    sim.check("Cmd+N with the toolbar clicked: a new, empty circuit",
              int(info["components"] == 0 and info["file"] == "" and not info["modified"]), 1, absolute=0,
              note=json.dumps(info)[:200])
    sim.cli("ui", "click", "--widget", "messages")
    reply = type_keys("cmd+o")
    sim.check("Cmd+O with Messages focused opens the Load Circuit dialog",
              int(closed_dialog(reply).endswith("[Load Circuit]")), 1, absolute=0, note=closed_dialog(reply))
    sim.cli("circuit", "open", path, "--discard")

    # A description still being typed is taken by a save, and the edit goes on (the circuit has
    # another change too: that the circuit is no longer modified shows the save happened)
    edit()
    sim.cli("ui", "focus", "none")
    sim.cli("view", "description", "--edit")
    sim.cli("ui", "type", "T", "y", "p", "e", "d", "--window")
    sim.cli("ui", "type", "cmd+s", "--window")
    d = sim.cli("circuit", "description")["description"]
    sim.check("Cmd+S in the description editor: the text typed so far is the circuit's", int(d == "Typed"), 1,
              absolute=0, note=d)
    sim.check("... and the circuit is saved (no longer modified)", int(not sim.cli("circuit", "info")["modified"]), 1,
              absolute=0)
    sim.cli("ui", "type", " ", "m", "o", "r", "e", "--window")
    sim.cli("ui", "focus", "canvas")
    sim.cli("ui", "type", "cmd+s")
    sim.cli("circuit", "open", path, "--discard")
    d = sim.cli("circuit", "description")["description"]
    sim.check("the edit went on; Cmd+S on the canvas saved the rest (reopened)", int(d == "Typed more"), 1,
              absolute=0, note=d)

    # In the code editor the file keys are the editor's own: they act on the file being edited
    src = os.path.join(tmp, "main.c")
    with open(src, "w") as f:
        f.write("int main(void)\n{\n    return 0;\n}\n")
    sim.cli("editor", "open", src)
    sim.cli("ui", "focus", "editor")
    sim.cli("ui", "type", " ", "x", "--window")
    doc = [d for d in sim.cli("editor", "files") if d["file"].endswith("main.c")][0]
    sim.check("the code editor has the keys (its document changed)", int(doc["modified"]), 1, absolute=0)
    before = r1_resistance(path)
    edit()
    sim.cli("ui", "type", "cmd+s", "--window")
    with open(src) as f:
        text = f.read()
    sim.check("Cmd+S in the code editor saves that file", int("x" in text and "return 0" in text), 1, absolute=0,
              note=text.replace("\n", " | "))
    not_saved("... and not the circuit", before)

    # The editor reads the keys in all of it (EditorWidget::keyPressEvent), not only in a document:
    # with the focus on its output pane they act on the file too. A review of #101 found them
    # saving the circuit there.
    sim.cli("ui", "focus", "editor")
    sim.cli("ui", "type", " ", "y", "--window")
    sim.cli("ui", "focus", "editor-output")
    edit()
    sim.cli("ui", "type", "cmd+s", "--window")
    with open(src) as f:
        text = f.read()
    sim.check("Cmd+S with the focus on the editor's output pane saves that file", int("x yint main" in text), 1, absolute=0,
              note=text.replace("\n", " | "))
    not_saved("... and not the circuit", before)

    docs = len(sim.cli("editor", "files"))
    sim.cli("ui", "type", "cmd+n", "--window")
    sim.check("Cmd+N with the focus on the editor's output pane opens a new document there",
              int(len(sim.cli("editor", "files")) == docs + 1), 1, absolute=0)
    not_saved("... and not a new circuit: this one is still open, unsaved", before)
    sim.cli("ui", "focus", "editor-output")           # The new document may have taken the focus
    reply = type_keys("cmd+o")
    sim.check("Cmd+O with the focus on the editor's output pane opens the editor's Load File dialog",
              int(closed_dialog(reply).endswith("[Load File]")), 1, absolute=0, note=closed_dialog(reply))
    not_saved("... not the circuit's", before)
    sim.cli("editor", "close", src, "--discard")


if __name__ == "__main__":
    run_test(test)
