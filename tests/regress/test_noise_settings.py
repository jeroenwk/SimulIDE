"""Transient noise settings (docs/NOISE_SPEC.md §3.1, §3.5, §6).

- Off by default: `sim status` says "noise": "off" and a saved file has no noise
  attribute, also after noise was switched on and off again (files without noise
  stay as they were).
- Every setting survives save and reopen.
- While noise is on, `sim status` shows the state, the seed in use and the number
  of sources; an entry of noiseOff that matches no part is reported.
- A change while running says it applies at the next start.
"""

import os
import re
import tempfile
from harness import run_test


def header(path):
    return re.search(r"<circuit[^>]*>", open(path, encoding="utf-8").read()).group(0)


def build(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=10")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("wire", "add", "B1.lPin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")


def test(sim):
    tmp = tempfile.mkdtemp(prefix="noise-settings-")
    build(sim)
    st = sim.cli("sim", "status")
    sim.check("noise off by default", 1 if st.get("noise") == "off" else 0, 1, absolute=0)

    p0 = os.path.join(tmp, "plain.sim1")
    sim.cli("circuit", "save", p0)
    sim.check("no noise attribute when off", 1 if "noise" in header(p0) else 0, 0, absolute=0)

    sim.cli("settings", "set", "noise=on")
    sim.cli("settings", "set", "noise=off")
    p1 = os.path.join(tmp, "onoff.sim1")
    sim.cli("circuit", "save", p1)
    sim.check("no noise attribute after on, off", 1 if "noise" in header(p1) else 0, 0, absolute=0)

    want = {"noise": True, "noiseBW": "2.5MHz", "noiseScale": 3.5, "noiseSeed": "123456789",
            "noiseStart": "100 us", "noiseStop": "2 ms", "noiseOff": "R1,type:Potentiometer"}
    sim.cli("settings", "set", "noise=on", "noiseBW=2.5MHz", "noiseScale=3.5", "noiseSeed=123456789",
            "noiseStart=100us", "noiseStop=2ms", "noiseOff=R1,type:Potentiometer")
    p2 = os.path.join(tmp, "noisy.sim1")
    sim.cli("circuit", "save", p2)
    sim.cli("circuit", "new", "--discard")
    sim.check("new circuit: noise off again", 1 if sim.cli("sim", "status")["noise"] == "off" else 0, 1, absolute=0)
    sim.cli("circuit", "open", p2, "--discard")
    for k, v in want.items():
        got = sim.cli("settings", "get", k)
        sim.check("reopened %s = %s (got %s)" % (k, v, got), 1 if got == v else 0, 1, absolute=0)

    # Running: status, seed in use, sources, unmatched entries
    sim.cli("settings", "set", "noiseSeed=0", "noiseStart=0", "noiseStop=0", "noiseOff=nosuchpart")
    sim.cli("sim", "run", "--for", "10us")
    n = sim.cli("sim", "status")["noise"]
    sim.check("status: active", 1 if n.get("active") else 0, 1, absolute=0)
    sim.check("status: random seed reported (nonzero)", 1 if int(n.get("seed", "0")) > 0 else 0, 1, absolute=0)
    sim.check("status: seedGiven false", 1 if n.get("seedGiven") is False else 0, 1, absolute=0)
    sim.check("status: 1 source (R1)", n.get("sources", -1), 1, absolute=0)
    sim.check("status: unmatched entry reported", 1 if n.get("unmatched") == ["nosuchpart"] else 0, 1, absolute=0)
    sim.check("status: summary says NOISE", 1 if n.get("summary", "").startswith("NOISE on") else 0, 1, absolute=0)
    msgs = "\n".join(sim.cli("messages")["lines"])
    sim.check("Messages: start line", 1 if "Noise on: 1 sources (1 thermal" in msgs else 0, 1, absolute=0)
    sim.check("Messages: noiseOff warning", 1 if "no part or type matches nosuchpart" in msgs else 0, 1, absolute=0)

    rows = sim.cli("settings", "set", "noiseScale=2")
    note = " ".join(str(r.get("note", "")) for r in rows)
    sim.check("change while running: note", 1 if "next sim start" in note else 0, 1, absolute=0)
    sim.cli("sim", "stop")


if __name__ == "__main__":
    run_test(test)
