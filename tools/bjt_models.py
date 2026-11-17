#!/usr/bin/env python3
"""Write resources/data/bjts.model (BJT part models) from SPICE .model lines.

SimulIDE's BJT (src/simulator/elements/active/e-bjt.cpp) is Ebers-Moll with Early: one saturation
current IS, one forward gain BF, no ISE/NE (low-current gain drop), IKF (high-current gain drop),
NF, RB/RE/RC or transit time. Copying a Gummel-Poon BF and IS would be wrong for many parts
(the Fairchild 2N4401 has BF = 4292 and an ISE that brings hFE down to ~200), so each part is
*fitted*: the full Gummel-Poon DC equations are solved at a reference collector current (VBC = 0,
27 C) and Gain = IC/IB there, IS = IC/(exp(VBE/Vt)-1) with VBE at the terminals (RB, RE drops
included). So hFE and VBE are right at that current and drift away from it the way a constant-
beta model does. VAF and the junction capacitances (CJE/VJE/MJE, CJC/VJC/MJC) are copied.

Input:  tools/bjt_models/standard_bjt_subset.lib (verbatim lines of LTspice's standard.bjt)
Output: resources/data/bjts.model     Run: python3 tools/bjt_models.py [--check]
"""
import math
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "tools" / "bjt_models" / "standard_bjt_subset.lib"
OUT = ROOT / "resources" / "data" / "bjts.model"
VT = 0.025865  # Thermal voltage used by eBJT (300 K)

def log(*a):
    print("[bjt_models]", *a, file=sys.stderr)

# Fit currents: small-signal parts at 1 mA (the bias of most book circuits), the 0.5-0.8 A
# switching parts at 10 mA, power parts at 100 mA.
SMALL, SWITCH, POWER = 1e-3, 10e-3, 100e-3

# (SimulIDE name, source .model, fit current, Vceo override, note)
PARTS = [
    # NPN
    ("2N2222",   "2N2222",   SWITCH, None, ""),
    ("2N2222A",  "2N2222A",  SWITCH, None, ""),
    ("2N3904",   "2N3904",   SMALL,  None, ""),
    ("2N4401",   "2N4401",   SWITCH, None, ""),
    ("2N5551",   "2N5551",   SMALL,  None, ""),
    ("BC107B",   "BC107B",   SMALL,  None, ""),
    ("BC109C",   "BC109C",   SMALL,  None, ""),
    ("BC337-25", "BC337-25", SWITCH, None, ""),
    ("BC337-40", "BC337-40", SWITCH, None, ""),
    ("BC546B",   "BC546B",   SMALL,  None, ""),
    ("BC546C",   "BC547C",   SMALL,  65,
        "no published model: BC546 is the 65 V selection of the BC547 die (LTspice's BC546B is its BC547B with Vceo 65), so the BC547C line is used"),
    ("BC547A",   "BC547A",   SMALL,  None, ""),
    ("BC547B",   "BC547B",   SMALL,  None, ""),
    ("BC547C",   "BC547C",   SMALL,  None, ""),
    ("BC548B",   "BC548B",   SMALL,  None, ""),
    ("BC548C",   "BC548C",   SMALL,  None, ""),
    ("BC550C",   "BC550C",   SMALL,  None, ""),
    ("BD135",    "BD135",    POWER,  None, ""),
    ("BD139",    "BD139",    POWER,  None, ""),
    ("TIP31C",   "TIP31C",   POWER,  None, ""),
    ("TIP41C",   "TIP41C",   POWER,  None, ""),
    ("2N3055",   "2N3055H",  POWER,  None, "LTspice's 2N3055H (Motorola) line"),
    # PNP
    ("2N2907",   "2N2907",   SWITCH, None, ""),
    ("2N2907A",  "2N2907P",  SWITCH, None,
        "LTspice's 2N2907P line (NSC-Philips): the National/Fairchild 2N2907A process model, complement of the 2N2222A line"),
    ("2N3906",   "2N3906",   SMALL,  None, ""),
    ("2N4403",   "2N4403",   SWITCH, None, ""),
    ("2N5401",   "2N5401",   SMALL,  None, ""),
    ("BC177",    "BC177",    SMALL,  None, ""),
    ("BC327-25", "BC327-25", SWITCH, None, ""),
    ("BC327-40", "BC327-40", SWITCH, None, ""),
    ("BC556B",   "BC556B",   SMALL,  None, ""),
    ("BC556C",   "BC557C",   SMALL,  65,
        "no published model: BC556 is the 65 V selection of the BC557 die (LTspice's BC556B is its BC557B with Vceo 65), so the BC557C line is used"),
    ("BC557A",   "BC557A",   SMALL,  None, ""),
    ("BC557B",   "BC557B",   SMALL,  None, ""),
    ("BC557C",   "BC557C",   SMALL,  None, ""),
    ("BC558B",   "BC558B",   SMALL,  None, ""),
    ("BC558C",   "BC558C",   SMALL,  None, ""),
    ("BC560C",   "BC560C",   SMALL,  None, ""),
    ("BD136",    "BD136",    POWER,  None, ""),
    ("BD140",    "BD140",    POWER,  None, ""),
    ("TIP32C",   "TIP32C",   POWER,  None, ""),
    ("TIP42C",   "TIP42C",   POWER,  None, ""),
]

SUFFIX = {"t": 1e12, "g": 1e9, "meg": 1e6, "k": 1e3, "m": 1e-3, "u": 1e-6, "n": 1e-9, "p": 1e-12, "f": 1e-15}
ALIAS = {"IK": "IKF", "PE": "VJE", "ME": "MJE", "PC": "VJC", "MC": "MJC", "VA": "VAF"}

def spice_num(s):
    m = re.match(r"^([-+]?(?:\d+\.?\d*|\.\d+)(?:e[-+]?\d+)?)(meg|[tgkmunpf])?", s.lower())
    if not m:
        raise ValueError(s)
    return float(m.group(1)) * SUFFIX.get(m.group(2) or "", 1)

def parse_lib(path):
    models = {}
    for line in path.read_text().splitlines():
        m = re.match(r"\.model\s+(\S+?)\s*\(?\s*(npn|pnp)\b(.*)", line, re.I)
        if not m:
            continue
        name, typ, rest = m.group(1).upper(), m.group(2).upper(), m.group(3)
        p = {}
        for k, v in re.findall(r"(\w+)\s*=\s*([^\s()]+)", rest):
            k = ALIAS.get(k.upper(), k.upper())
            if k == "MFG":
                p[k] = v
                continue
            try:
                p[k] = spice_num(v)  # A repeated key: the last one wins, as in SPICE
            except ValueError:
                log(f"{name}: {k}={v} not a number, ignored")
        models[name] = (typ, p, line)
    return models

def gummel_poon(p, vbe):
    """Forward-active DC currents at internal VBE, VBC = 0: (IC, IB)."""
    IS, BF, NF = p.get("IS", 1e-16), p.get("BF", 100), p.get("NF", 1)
    ISE, NE = p.get("ISE", 0), p.get("NE", 1.5)
    IKF, VAR = p.get("IKF", 0), p.get("VAR", 0)
    i_f = IS * (math.exp(vbe / (NF * VT)) - 1)
    q1 = 1 / (1 - vbe / VAR) if VAR > 0 else 1
    q2 = i_f / IKF if IKF > 0 else 0
    qb = q1 / 2 * (1 + math.sqrt(1 + 4 * q2))
    ic = i_f / qb
    ib = i_f / BF + ISE * (math.exp(vbe / (NE * VT)) - 1)
    return ic, ib, qb

def base_res(p, ib, qb):
    """Gummel-Poon base resistance: RB at low current, falling to RBM (with IRB, or as 1/qb)."""
    RB = p.get("RB", 0)
    RBM = p.get("RBM", RB)
    IRB = p.get("IRB", 0)
    if IRB <= 0:
        return RBM + (RB - RBM) / qb
    x = 144 / math.pi**2 * ib / IRB
    z = (-1 + math.sqrt(1 + x)) / (24 / math.pi**2 * math.sqrt(ib / IRB))
    t = math.tan(z)
    return RBM + 3 * (RB - RBM) * (t - z) / (z * t * t)

def fit(p, ic_target):
    lo, hi = 0.0, 1.5
    for _ in range(200):
        mid = (lo + hi) / 2
        if gummel_poon(p, mid)[0] < ic_target: lo = mid
        else: hi = mid
    vbe = (lo + hi) / 2
    ic, ib, qb = gummel_poon(p, vbe)
    vbe_ext = vbe + ib * base_res(p, ib, qb) + (ib + ic) * p.get("RE", 0)
    gain = ic / ib
    is_eff = ic / (math.exp(vbe_ext / VT) - 1)
    return gain, is_eff, vbe_ext

def g(x):
    return f"{x:.4g}"

def build():
    models = parse_lib(SRC)
    log(f"{len(models)} source models read from {SRC.relative_to(ROOT)}")
    groups = {"NPN": [], "PNP": []}
    for name, src, ic, vceo, note in PARTS:
        typ, p, line = models[src.upper()]
        gain, is_eff, vbe = fit(p, ic)
        cap = lambda k: p.get(k, 0) * 1e12
        attrs = (f'name="{name}" gain="{g(gain)}" is="{g(is_eff)}" va="{g(p.get("VAF", 0))}" '
                 f'cje="{g(cap("CJE"))}" vje="{g(p.get("VJE", 0.75))}" mje="{g(p.get("MJE", 0.33))}" '
                 f'cjc="{g(cap("CJC"))}" vjc="{g(p.get("VJC", 0.75))}" mjc="{g(p.get("MJC", 0.33))}"')
        v = vceo if vceo else p.get("VCEO")
        info = (f"{src}" + (f" ({p['MFG']})" if "MFG" in p else "") +
                f": fit at IC {g(ic*1e3)} mA, VBE {vbe:.3f} V, SPICE BF {g(p.get('BF', 100))}" +
                (f", Vceo {g(v)} V" if v else "") + (f"; {note}" if note else ""))
        log(f"{name:9s} {typ} gain {gain:7.1f}  IS {is_eff:.3e}  VBE {vbe:.3f} V @ {ic*1e3:g} mA  (SPICE BF {p.get('BF', 100):g})")
        groups[typ].append(f'        <item {attrs} /><!-- {info} -->')

    head = """<!DOCTYPE SimulIDE>

<!-- BJT models for the BJT component (e-bjt.cpp). GENERATED by tools/bjt_models.py from the
     verbatim LTspice standard.bjt lines in tools/bjt_models/standard_bjt_subset.lib; edit those, not this file.
     SimulIDE's BJT has one gain and one IS (no ISE/NE/IKF/RB/RE/TF), so gain and is are fitted to the
     full Gummel-Poon model at the IC in each comment: hFE and VBE are right there, the gain stays
     constant elsewhere. gain = BF, is (A), va = Early voltage VAF (V, 0 = none),
     cje/cjc (pF) vje/vjc (V) mje/mjc = junction capacitances. Vceo is not applied (a rating, not a breakdown). -->

<itemlib>"""
    out = [head]
    for typ in ("NPN", "PNP"):
        out.append(f'    <itemset type="{typ}">')
        out += groups[typ]
        out.append("    </itemset>")
    out.append("</itemlib>")
    return "\n".join(out) + "\n"

if __name__ == "__main__":
    text = build()
    if "--check" in sys.argv:
        same = OUT.exists() and OUT.read_text() == text
        log("up to date" if same else f"{OUT.relative_to(ROOT)} is out of date")
        sys.exit(0 if same else 1)
    OUT.write_text(text)
    log(f"wrote {OUT.relative_to(ROOT)}")
