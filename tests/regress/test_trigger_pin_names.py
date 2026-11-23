"""Trigger pin names for polarized parts (specs/triggers.html §11).

A trigger names unlabelled pins by number ("C1 pin 1"), and the canvas does not show which end of
an electrolytic capacitor is pin 1 by number. Polarized parts now use their polarity (C1 pin +,
B1 pin −, C1 current (+→−), "C1 + to −" across), meters + / COM, the op amp In+ In− Out V+ V−
(its labels show "+" twice), PUT A K G, UJT E B1 B2; diodes kept A / K. A hyphen is accepted for
− and for the – of pin labels (C1.-, U1.In-, M1.-).
"""

from harness import run_test


def watch(sim, w, cond="rise:1"):
    return sim.cli("trigger", "add", w, cond)["watches"]


def same(sim, name, got, want):
    sim.check("%s: %s" % (name, want), 1 if got == want else 0, 1, absolute=0, note=got)


def test(sim):
    sim.cli("circuit", "new", "--discard")
    for kind, label, x in (("elCapacitor", "C1", 0), ("Battery", "B1", 80), ("opAmp", "U1", 160),
                           ("Voltimeter", "VM", 240), ("Diode", "D1", 320), ("DcMotor", "M1", 400),
                           ("PUT", "P1", 480), ("UJT", "Q1", 560), ("Capacitor", "C2", 640)):
        sim.cli("comp", "add", kind, "--label", label, "--at", "%d,0" % x)
    same(sim, "C1.+", watch(sim, "C1.+"), "C1 pin +")
    same(sim, "C1.- (hyphen)", watch(sim, "C1.-"), "C1 pin −")
    same(sim, "C1.2 (number)", watch(sim, "C1.2"), "C1 pin −")
    same(sim, "C1 across", watch(sim, "C1"), "C1 + to −")
    same(sim, "C1:i", watch(sim, "C1:i", "rise:1m"), "C1 current (+→−)")
    same(sim, "B1.1", watch(sim, "B1.1"), "B1 pin +")
    same(sim, "U1 inputs", watch(sim, "U1.In+-U1.In-", "cross:0"), "U1 pins In+–In− (V)")
    same(sim, "U1.V-", watch(sim, "U1.V-", "fall:-1"), "U1 pin V−")
    same(sim, "U1.Out", watch(sim, "U1.Out"), "U1 pin Out")
    same(sim, "VM.COM", watch(sim, "VM.COM"), "VM pin COM")
    same(sim, "D1.K (unchanged)", watch(sim, "D1.K"), "D1 pin K")
    same(sim, "D1:i (unchanged)", watch(sim, "D1:i", "rise:1m"), "D1 current (A→K)")
    same(sim, "M1.- (its label –)", watch(sim, "M1.-"), "M1 pin 2 –")
    same(sim, "P1.G", watch(sim, "P1.G"), "P1 pin G")
    same(sim, "Q1.B1", watch(sim, "Q1.B1"), "Q1 pin B1")
    same(sim, "C2 (not polarized)", watch(sim, "C2"), "C2 pins 1–2 (V)")
    xml = [t for t in sim.cli("trigger", "list") if t["watches"] == "C1 + to −"]
    sim.check("listed", len(xml), 1, absolute=0)


if __name__ == "__main__":
    run_test(test)
