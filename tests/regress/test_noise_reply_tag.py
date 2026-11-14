"""Transient noise: measurement replies say when their numbers depend on noise
(docs/NOISE_SPEC.md §3.5 item 4).

Circuit: 10 V → R1 1 kΩ → node → R2 1 kΩ ∥ C1 1 nF → ground, an oscilloscope on the node.
- Noise off: `probe`, `read` and `scope data` replies have no `noise` field.
- Noise on, window 0–200 µs: inside the window all three carry it.
- After the window: `scope data` over the last 100 µs (only noise-free samples) has no
  tag, over the whole run it has one; `probe` and `read` keep it (state "ended"):
  an instant value after the window still follows from the noise drawn (a bistable's
  state), so it is tagged for the rest of the run.
"""

from harness import run_test


def build(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B1", "--at", "-80,0", "Voltage=10")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,120")
    c("comp", "add", "Resistor", "--label", "R1", "--at", "0,-40", "Resistance=1000")
    c("comp", "add", "Resistor", "--label", "R2", "--at", "80,40", "--rot", "90", "Resistance=1000")
    c("comp", "add", "Capacitor", "--label", "C1", "--at", "160,40", "--rot", "90", "Capacitance=1nF")
    c("comp", "add", "Oscope", "--label", "SC", "--at", "320,0")
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "R2.lPin")
    c("wire", "add", "R2.lPin", "C1.lPin")
    c("wire", "add", "R2.rPin", "G.Gnd")
    c("wire", "add", "C1.rPin", "G.Gnd")
    c("wire", "add", "R2.lPin", "SC.Pin0")
    c("scope", "set", "SC", "--timediv", "10us", "--trigger", "none")
    c("settings", "set", "reactStep=5ns", "noiseBW=100MHz", "noiseScale=1000", "noiseSeed=5")


def tags(sim, last=None):
    probe = sim.cli("probe", "R2.lPin")[0]
    read = sim.cli("read", "R1")
    args = ["scope", "data", "SC", "--max", "50"] + (["--last", last] if last else [])
    scope = sim.cli(*args)
    return ("noise" in probe, "noise" in read, "noise" in scope), probe, read, scope


def flag(sim, name, got, want):
    sim.check(name, 1 if got else 0, 1 if want else 0, absolute=0)


def test(sim):
    build(sim)
    sim.cli("settings", "set", "noise=off")
    sim.cli("sim", "run", "--for", "100us")
    (p, r, s), *_ = tags(sim)
    flag(sim, "noise off: probe untagged", p, False)
    flag(sim, "noise off: read untagged", r, False)
    flag(sim, "noise off: scope data untagged", s, False)
    sim.cli("sim", "stop")

    sim.cli("settings", "set", "noise=on", "noiseStop=200us")
    sim.cli("sim", "run", "--for", "100us", timeout=600)
    (p, r, s), probe, _, _ = tags(sim)
    flag(sim, "inside the window: probe tagged", p, True)
    flag(sim, "inside the window: read tagged", r, True)
    flag(sim, "inside the window: scope data tagged", s, True)
    flag(sim, "inside the window: state on", probe["noise"].get("state") == "on", True)

    sim.cli("sim", "run", "--for", "300us", timeout=600)   # now 400 µs, window ended at 200 µs
    (p, r, s), probe, _, _ = tags(sim, last="100us")
    flag(sim, "after the window: scope data of the last 100 us untagged", s, False)
    flag(sim, "after the window: probe still tagged", p, True)
    flag(sim, "after the window: read still tagged", r, True)
    flag(sim, "after the window: probe says ended", probe["noise"].get("state") == "ended", True)
    (_, _, s), *_ = tags(sim)
    flag(sim, "after the window: scope data over the whole run tagged", s, True)

    sim.cli("sim", "stop")
    sim.cli("settings", "set", "noise=off")
    sim.cli("sim", "run", "--for", "100us")
    (p, r, s), *_ = tags(sim)
    flag(sim, "next run without noise: probe untagged", p, False)
    flag(sim, "next run without noise: scope data untagged", s, False)


if __name__ == "__main__":
    run_test(test)
