"""Transient noise seed and time window (docs/NOISE_SPEC.md §3.1, §5.4, §5.5).

Circuit: 10 V → R1 1 kΩ → node → R2 1 kΩ ∥ C1 1 nF → ground (noise-free: 5 V).
- The same seed gives exactly the same run; another seed a different one.
- Seed 0 picks a random seed, reports it in `sim status`, and that seed repeats the run.
- noiseStop = 200 µs: after it every noise current is 0 again, so from 300 µs on the
  node is exactly the value of a run without noise (the RC of 0.5 µs has decayed by e^-200: no
  measurable noise); the state is "ended", and an average taken after the window
  has no noise tag, one taken inside the window has it.
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
    c("wire", "add", "B1.rPin", "G.Gnd")
    c("wire", "add", "B1.lPin", "R1.lPin")
    c("wire", "add", "R1.rPin", "R2.lPin")
    c("wire", "add", "R2.lPin", "C1.lPin")
    c("wire", "add", "R2.rPin", "G.Gnd")
    c("wire", "add", "C1.rPin", "G.Gnd")
    c("settings", "set", "reactStep=5ns", "noise=on", "noiseBW=100MHz", "noiseScale=1000")


def run(sim, seed, span="100us"):
    sim.cli("sim", "stop")
    sim.cli("settings", "set", "noiseSeed=%s" % seed)
    r = sim.cli("sim", "average", "--for", span, "R2.lPin", timeout=600)
    v = r["channels"][0]["voltage"]
    return (v["mean"], v["rms"], v["min"], v["max"]), r


def test(sim):
    build(sim)
    sim.cli("settings", "set", "noise=off")
    sim.cli("sim", "run", "--for", "50us")
    clean = sim.cli("probe", "R2.lPin")[0]["voltage"]   # 5 V less the battery's 1 mΩ share
    sim.cli("settings", "set", "noise=on")
    a, _ = run(sim, 7)
    b, _ = run(sim, 7)
    c, _ = run(sim, 8)
    sim.check("seed 7 twice: identical", 1 if a == b else 0, 1, absolute=0)
    sim.check("seed 8 differs from 7", 1 if a != c else 0, 1, absolute=0)

    d, _ = run(sim, 0)
    used = sim.cli("sim", "status")["noise"]["seed"]
    sim.check("seed 0: a random seed is reported", 1 if int(used) > 0 else 0, 1, absolute=0)
    e, _ = run(sim, used)
    sim.check("the reported seed repeats the run", 1 if d == e else 0, 1, absolute=0)

    sim.cli("sim", "stop")
    sim.cli("settings", "set", "noiseSeed=3", "noiseStop=200us")
    r_in = sim.cli("sim", "average", "--for", "150us", "R2.lPin", timeout=600)
    sim.check("inside the window: reply has the noise tag", 1 if "noise" in r_in else 0, 1, absolute=0)
    sim.cli("sim", "run", "--for", "150us")
    st = sim.cli("sim", "status")["noise"]
    sim.check("after the window: state ended", 1 if st.get("state") == "ended" else 0, 1, absolute=0)
    sim.check("after the window: active false", 1 if st.get("active") is False else 0, 1, absolute=0)
    r = sim.cli("sim", "average", "--for", "100us", "R2.lPin", timeout=600)
    v = r["channels"][0]["voltage"]
    sim.check("after the window: no noise tag", 1 if "noise" in r else 0, 0, absolute=0)
    sim.check("after the window: the noise-free value, exactly", v["mean"], clean, absolute=0)
    sim.check("after the window: no variation (max-min)", v["max"] - v["min"], 0, absolute=1e-12)
    sim.cli("sim", "stop")


if __name__ == "__main__":
    run_test(test)
