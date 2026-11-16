"""CLI values and settings from the create-circuit triage of 2026-11-15 (issues #225, #234,
#236, #243, #244, #224, #259, #266).

- A percent or degree property (WaveGen Duty "_%", Phase "_º") takes "85%" / "90°" as typed.
- "1meg" is 1 MΩ (SPICE), not 1 m + the unit "eg".
- An unsigned integer property with a unit (Thermistor R25, "uint") takes "10k" = 10000 Ω,
  not 0 (the value used to pass unparsed into an integer setter).
- A Model given on the same comp add line as other settings is applied first, so the
  settings next to it stay (BrkDownV 6.2 V with "Zener Default", whose own is 5.6 V).
- read of a Diode gives its voltage and current: 5 V through 1 kΩ and a 1N4148-like diode
  gives about (5 - 0.65) / 1000 = 4.3 mA; checked against the resistor's own current.
- A command that takes no name=value settings refuses them instead of dropping them.
"""

from harness import run_test
from simctl import SimError


def prop(sim, comp, name):
    d = sim.cli("comp", "get", comp)
    for p in d.get("properties", []):
        if p["name"] == name:
            return p["value"]
    raise AssertionError("no property %s on %s" % (name, comp))


def num(s):
    v, unit = s.split(" ", 1) if " " in s else (s, "")
    mult = {"k": 1e3, "M": 1e6, "m": 1e-3, "µ": 1e-6}.get(unit[:1], 1) if len(unit) > 1 else 1
    return float(v) * mult


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "WaveGen", "--label", "W", "Duty=85%", "Phase=90°", "Freq=1meg")
    sim.check("Duty 85%", num(prop(sim, "W", "Duty")), 85, tol=0.001)
    sim.check("Phase 90°", num(prop(sim, "W", "Phase")), 90, tol=0.001)
    sim.check("Freq 1meg", num(prop(sim, "W", "Freq")), 1e6, tol=0.001)

    sim.cli("comp", "add", "Thermistor", "--label", "TH", "R25=10k")
    sim.check("R25 10k", num(prop(sim, "TH", "R25")), 10000, tol=0.001)

    sim.cli("comp", "add", "Zener", "--label", "Z", "Model=Zener Default", "BrkDownV=6.2V")
    sim.check("BrkDownV kept next to Model", num(prop(sim, "Z", "BrkDownV")), 6.2, tol=0.001)

    refused = False
    try:
        sim.cli("comp", "add", "Oscope", "--label", "SC")
        sim.cli("scope", "set", "SC", "--timediv", "1ms", "InputImped=1G")
    except SimError as e:
        refused = "no name=value" in str(e)
    sim.check("scope set refuses InputImped=1G", 1 if refused else 0, 1, absolute=0)

    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "Battery", "--label", "B1", "--at", "0,0", "Voltage=5")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=1000")
    sim.cli("comp", "add", "Diode", "--label", "D1", "--at", "160,-40")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "0,80")
    sim.cli("wire", "add", "B1.lPin", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "D1.lPin")
    sim.cli("wire", "add", "D1.rPin", "G.Gnd")
    sim.cli("wire", "add", "B1.rPin", "G.Gnd")
    sim.run("10ms")
    r1, d1 = sim.read("R1"), sim.read("D1")
    sim.check("D1 current = R1 current", d1.get("current", 0), abs(r1["current"]), tol=0.01)
    sim.check("D1 forward voltage", d1.get("voltage", 0), 0.65, tol=0.25)


if __name__ == "__main__":
    run_test(test)
