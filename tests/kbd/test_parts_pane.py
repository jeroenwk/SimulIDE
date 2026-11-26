"""Parts pane, BOM fields and BOM export (specs/parts-pane.md §1-§9).

Circuit: resistors R1, R2, R10 (same value), R3 (another value, a changed setting), a BJT Q1
with a model, a subcircuit IC1 (74HC00), a scope, a fixed voltage, a rectangle and a tunnel.
Expected values follow the spec:
- default filters leave out graphical parts, meters and sources; tunnels and buses go with
  the meters (not bought parts), sockets and headers are shown; BOM
  order R1, R2, R3, R10 (natural); the subcircuit is one row;
- Type as in the component list, Value as on the canvas, Other values = every changed setting;
- BOM fields edited on several parts are one undo step, saved in the .sim1 only when set;
- the Tab column is the canvas Tab walk; sorting by value is numeric;
- search 10k / BC; filters; selection both ways; grouped CSV / Markdown BOM.
Nothing is simulated.
"""

import csv
import io
import os
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "regress"))
from harness import run_test  # noqa: E402


def ok(sim, name, cond, note=""):
    sim.check(name, int(bool(cond)), 1, absolute=0, note=str(note)[:300])


def labels(rows):
    return [r["label"] for r in rows]


def row(rows, label):
    for r in rows:
        if r["label"] == label:
            return r
    raise AssertionError("no row " + label)


def build(sim):
    sim.cli("circuit", "new", "--discard")
    for lab, x in (("R10", 100), ("R2", 200), ("R1", 300)):
        sim.cli("comp", "add", "Resistor", "--label", lab, "--at", "%d,100" % x, "Resistance=10k")
    sim.cli("comp", "add", "Resistor", "--label", "R3", "--at", "400,100", "Resistance=4.7k", "Cp=2p")
    q = sim.cli("comp", "add", "BJT", "--label", "Q1", "--at", "100,200")["uid"]
    models = [m for m in sim.cli("comp", "get", q, "--all")["properties"] if m["name"] == "Model"]
    sim.cli("comp", "add", "74HC00", "--label", "IC1", "--at", "250,220")
    sim.cli("comp", "add", "Oscope", "--label", "SC1", "--at", "500,300")
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V1", "--at", "100,300")
    sim.cli("comp", "add", "Rectangle", "--label", "BG", "--at", "600,100")
    sim.cli("comp", "add", "Tunnel", "--label", "T1", "--at", "400,300")
    sim.cli("comp", "add", "Header", "--label", "J1", "--at", "600,300")
    return q, models


def test(sim):
    q, models = build(sim)
    opts = models[0].get("options") or models[0].get("enum") or [] if models else []
    model = next((m for m in opts if m not in ("Custom", "")), None) or "2N3904"
    sim.cli("comp", "set", "Q1", "Model=" + model)

    # 1. Default view
    rows = sim.cli("parts", "list")
    ok(sim, "default: no graphical, meter, source", not {"BG", "SC1", "V1"} & set(labels(rows)), labels(rows))
    ok(sim, "default: no tunnel (with the meters)", "T1" not in labels(rows), labels(rows))
    ok(sim, "BOM order natural", labels(rows) == ["IC1", "J1", "Q1", "R1", "R2", "R3", "R10"], labels(rows))
    ok(sim, "default: a header is shown", "J1" in labels(rows), labels(rows))
    ok(sim, "subcircuit one row", labels(rows).count("IC1") == 1 and not any("IC1" != r["label"] and r["type"] == "74HC00" for r in rows), rows)

    # 2. Columns
    r1 = row(rows, "R1")
    ok(sim, "type as the left pane", r1["type"] == "Resistor" and row(rows, "IC1")["type"] == "74HC00", (r1["type"], row(rows, "IC1")["type"]))
    ok(sim, "value as on the canvas", r1["value"] == "10 kΩ", r1["value"])
    ok(sim, "default part: no other values", r1["other"] == "", r1["other"])
    ok(sim, "changed setting in other values", "2 pF" in row(rows, "R3")["other"], row(rows, "R3")["other"])
    ok(sim, "model column", row(rows, "Q1")["model"] == model, row(rows, "Q1"))

    # 2b. BOM fields: one undo step for three parts, saved, not written when empty
    r = sim.cli("parts", "set", "R1,R2,R10", "mpn=CFR-25JB-52-10K", "mfr=Yageo")
    ok(sim, "parts set: three parts", r["changed"] == 3, r)
    rows = sim.cli("parts", "list")
    ok(sim, "MPN on R10", row(rows, "R10")["mpn"] == "CFR-25JB-52-10K", row(rows, "R10"))
    sim.cli("undo")
    rows = sim.cli("parts", "list")
    ok(sim, "one undo step clears all three", all(row(rows, l)["mpn"] == "" for l in ("R1", "R2", "R10")), rows)
    sim.cli("redo")
    sim.cli("comp", "set", "Q1", "bomMpn=BC547BTA")
    d = tempfile.mkdtemp(prefix="parts-")
    path = os.path.join(d, "pp.sim1")
    sim.cli("circuit", "save", path)
    xml = open(path, encoding="utf-8").read()
    ok(sim, "saved BOM field", 'bomMpn="CFR-25JB-52-10K"' in xml and 'bomMpn="BC547BTA"' in xml)
    r3 = [l for l in xml.splitlines() if 'label="R3"' in l]
    ok(sim, "no bom attribute when empty", r3 and "bom" not in r3[0], r3)
    sim.cli("circuit", "open", path, "--discard")
    rows = sim.cli("parts", "list")
    ok(sim, "kept after reopen", row(rows, "R2")["mfr"] == "Yageo" and row(rows, "Q1")["mpn"] == "BC547BTA", rows)

    # 3. Tab column = Tab walk; a move updates it
    allrows = sim.cli("parts", "list", "--all", "--sort", "tab")
    walk = []
    sim.cli("select", "--none")
    sim.cli("ui", "click", "-300,-300")
    for _ in range(len(allrows)):
        s = sim.cli("ui", "type", "tab")
        walk.append(s["selection"][0] if s["selection"] else None)
    first = walk.index(allrows[0]["uid"]) if allrows[0]["uid"] in walk else 0
    walk = walk[first:] + walk[:first]
    ok(sim, "Tab column = Tab walk", [r["uid"] for r in allrows] == walk, ([r["label"] for r in allrows], walk))
    t_before = row(sim.cli("parts", "list"), "R10")["tab"]
    sim.cli("comp", "move", "R10", "700,100")
    t_after = row(sim.cli("parts", "list"), "R10")["tab"]
    ok(sim, "move updates Tab", t_before != t_after, (t_before, t_after))

    # 4. Sorting
    vals = [r["label"] for r in sim.cli("parts", "list", "--sort", "value") if r["label"].startswith("R")]
    ok(sim, "sort by value numeric", vals.index("R3") < vals.index("R1"), vals)
    rev = [r["label"] for r in sim.cli("parts", "list", "--sort", "-label")]
    ok(sim, "reversed", rev[0] == "R10" and rev[-1] == "IC1", rev)
    tabs = [r["tab"] for r in sim.cli("parts", "list", "--sort", "tab")]
    ok(sim, "sort by tab", tabs == sorted(tabs), tabs)

    # 5. Search and filters
    ok(sim, "search 10k", sorted(labels(sim.cli("parts", "list", "--search", "10k"))) == ["R1", "R10", "R2"])
    ok(sim, "search 10000", sorted(labels(sim.cli("parts", "list", "--search", "10000"))) == ["R1", "R10", "R2"])
    ok(sim, "search model", labels(sim.cli("parts", "list", "--search", model)) == ["Q1"])
    ok(sim, "filter meters", {"SC1", "T1"} <= set(labels(sim.cli("parts", "list", "--filter", "meters"))))
    ok(sim, "filter sources", "V1" in labels(sim.cli("parts", "list", "--filter", "sources")))
    ok(sim, "filter graphical", "BG" in labels(sim.cli("parts", "list", "--filter", "graphical")))
    ok(sim, "--all", len(sim.cli("parts", "list", "--all")) == 11)

    # 6. Selection both ways (the pane's own rows)
    sim.cli("view", "parts", "--filter", "default")     # The user's settings may have other toggles
    sim.cli("select", "R1", "R2")
    sel = sim.cli("parts", "list", "--selected")
    ok(sim, "canvas -> pane", sorted(labels(sel)) == ["R1", "R2"], labels(sel))
    sim.cli("select", "R1", "SC1")
    r = sim.cli_full("parts", "list", "--selected") if hasattr(sim, "cli_full") else None
    sel = sim.cli("parts", "list", "--selected")
    ok(sim, "filtered-out selected part not a row", labels(sel) == ["R1"], labels(sel))
    v = sim.cli("view", "parts")
    ok(sim, "count line says hidden", "1 hidden by filters" in v["count"], v["count"])
    sim.cli("select", "--none")
    v = sim.cli("view", "parts", "--show", "Q1")
    ok(sim, "pane -> canvas", [x["label"] for x in sim.cli("select")] == ["Q1"], v)
    sim.cli("view", "parts", "--search", "")

    # 7. BOM
    text = sim.cli("parts", "bom")
    rd = list(csv.reader(io.StringIO(text)))
    ok(sim, "CSV header", rd[0][:3] == ["Qty", "Labels", "Type"], rd[0])
    grp = [l for l in rd if l[1] == "R1, R2, R10"]
    ok(sim, "grouped 3 resistors", grp and grp[0][0] == "3" and grp[0][4] == "10 kΩ", rd)
    ok(sim, "filtered parts left out", not any("SC1" in l[1] or "V1" in l[1] or "BG" in l[1] for l in rd), rd)
    per = list(csv.reader(io.StringIO(sim.cli("parts", "bom", "--per-part"))))
    ok(sim, "per part", per[0][0] == "Label" and len(per) == len(rd) + 2, (len(per), len(rd)))
    out = os.path.join(d, "pp_bom.csv")
    r = sim.cli("parts", "bom", "--out", out)
    raw = open(out, "rb").read()
    ok(sim, "file starts with UTF-8 BOM", raw.startswith(b"\xef\xbb\xbf"), raw[:10])
    back = list(csv.reader(io.StringIO(raw.decode("utf-8-sig"))))
    ok(sim, "Ω survives a CSV round trip", any(l[4] == "10 kΩ" for l in back[1:]), back)
    md = sim.cli("parts", "bom", "--format", "md")
    ok(sim, "markdown table", "| 3 | R1, R2, R10 |" in md, md[:300])

    # 8. Live rows: add, delete, undo, paste, another circuit
    n = len(sim.cli("parts", "list", "--all"))
    sim.cli("comp", "add", "Capacitor", "--label", "C1", "--at", "100,400")
    ok(sim, "add", len(sim.cli("parts", "list", "--all")) == n + 1)
    sim.cli("comp", "remove", "C1")
    ok(sim, "delete", len(sim.cli("parts", "list", "--all")) == n)
    sim.cli("undo")
    ok(sim, "undo", "C1" in labels(sim.cli("parts", "list", "--all")))
    sim.cli("comp", "copy", "R1")
    ok(sim, "paste", len(sim.cli("parts", "list", "--all")) == n + 2)
    sim.cli("circuit", "new", "--discard")
    ok(sim, "another circuit", sim.cli("parts", "list", "--all") == [])
    v = sim.cli("view", "parts")
    ok(sim, "pane follows the new circuit", v["shown"] == 0, v)


if __name__ == "__main__":
    run_test(test)
