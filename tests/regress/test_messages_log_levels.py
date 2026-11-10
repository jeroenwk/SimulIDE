"""Messages pane log levels: debug logs only with Log: Verbose on.

Developer debug logs are qDebug() lines starting with a "[tag]" ([labels], [ScopeTrig],
[plot-clip], ...). With many of them the Messages pane became unreadable. Now:
- Verbose off (settings logVerbose=false): no "[tag]" line in Messages; upstream's
  messages for the user ("Starting Circuit Simulation...") still are;
- Verbose on: the debug lines are there too ([labels] from adding a part).
Untagged Qt warnings (the offscreen plugin's "propagateSizeHints") stay off the pane.
The setting is in the user settings: the test restores the value it found.
"""

import re

from harness import run_test

TAG = re.compile(r"^\[[^\]\n]{1,48}\]")


def tagged(lines):
    return [l for l in lines if TAG.match(l)]


def test(sim):
    was = sim.cli("settings", "get", "logVerbose")
    was = was.get("logVerbose", was) if isinstance(was, dict) else was
    sim.log("logVerbose was %s" % was)
    try:
        sim.cli("settings", "set", "logVerbose=false")
        sim.cli("circuit", "new", "--discard")
        sim.cli("comp", "add", "WaveGen", "--label", "GEN", "--at", "0,0",
                "Wave_Type=Sine", "Freq=1 kHz", "Semi_Ampli=1 V", "Bipolar=true", "Always_On=true", "Show_id=true")
        sim.cli("comp", "add", "Oscope", "--label", "SCOPE", "--at", "200,0")
        sim.cli("comp", "add", "Ground", "--label", "G", "--at", "40,80")
        sim.cli("wire", "add", "GEN.outnod", "SCOPE.Pin0")
        sim.cli("wire", "add", "GEN.gndnod", "G.Gnd")
        sim.cli("wire", "add", "SCOPE.PinG", "G.Gnd")
        sim.cli("scope", "set", "SCOPE", "--timediv", "500us", "--trigger", "0", "--triglevel", "0.2")
        sim.cli("sim", "run", "--for", "20ms")

        lines = sim.cli("messages")["lines"]
        dbg = tagged(lines)
        sim.log("verbose off: %d lines, %d tagged %s" % (len(lines), len(dbg), dbg[:3]))
        sim.check("verbose off: no debug lines", len(dbg), 0, absolute=0)
        sim.check("verbose off: info still shown", int(any("Starting Circuit Simulation" in l for l in lines)), 1, absolute=0)
        sim.check("verbose off: no untagged Qt warnings",
                  int(any("propagateSizeHints" in l for l in lines)), 0, absolute=0)

        sim.cli("settings", "set", "logVerbose=true")
        sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "0,160", "Show_id=true")
        sim.cli("sim", "run", "--for", "5ms")          # Lets the deferred label layout run and log
        lines = sim.cli("messages")["lines"]
        dbg = tagged(lines)
        sim.log("verbose on: %d tagged, e.g. %s" % (len(dbg), dbg[:2]))
        sim.check("verbose on: debug lines shown", int(any(l.startswith("[labels]") for l in dbg)), 1, absolute=0)
    finally:
        sim.cli("settings", "set", "logVerbose=%s" % ("true" if str(was).lower() == "true" else "false"))


if __name__ == "__main__":
    run_test(test)
