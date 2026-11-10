"""BH-052: with the trigger level on Auto, a burst signal shows the flat gap, not the burst.

Auto level places the screen from the channel's measured period: the time between the
last two crossings of the middle of the last cycle. On a burst (a ringing LC hit by a
square wave) that "period" runs from the end of one ring to the start of the next, many
screens long, so the screen lands in the flat gap between bursts. A fixed level (e.g.
0.1 V) shows the ring.

Circuit: 5 kHz +-5 V square through 100 kOhm into 392 uH || 22.6 pF || 10 MOhm (the
led_driver T1 ring test): every edge rings at about 1.69 MHz for a few us, with about
+-0.4 V (I*sqrt(L/C)), then 100 us of nothing. Scope at 1 us/div, 0.1 V/div, noise filter
0.1 V (as in the saved circuit), trigger CH1, level Auto. Speed 0.01 %: a GUI frame
every 5 us of simulated time, as when watching it in the GUI (at higher speeds the
screen depends on where the last frames fall and the bug comes and goes).

Right result: like a real scope in Auto level, the trigger sits at the middle of the
signal's range and the screen shows a burst: the trace swings over most of the ring's
peak-to-peak range, not a flat line.
"""

import os
import re
import tempfile

from harness import run_test

TRACE = "#c8b80a"   # Light theme, channel 1


def trace_points(sim):
    svg = os.path.join(tempfile.mkdtemp(prefix="bh052-"), "scope.svg")
    sim.cli("export", "--file", svg, "--format", "svg", "--theme", "Light", "--meters", "show")
    text = open(svg, encoding="utf-8").read()
    pts = []
    for a, body in re.findall(r'<g ([^>]*)>(.*?)</g>', text, re.S):
        if 'stroke="%s"' % TRACE in a.lower():
            for p in re.findall(r'points="([^"]*)"', body):
                pts += [tuple(map(float, q.split(","))) for q in p.split()]
    return pts


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "WaveGen", "--label", "GEN", "--at", "0,0",
            "Wave_Type=Square", "Freq=5 kHz", "Semi_Ampli=5 V", "Bipolar=true", "Always_On=true")
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "80,-40", "Resistance=100 kΩ")
    sim.cli("comp", "add", "Inductor", "--label", "L", "--at", "160,40", "--rotate", "90", "Inductance=392 µH")
    sim.cli("comp", "add", "Capacitor", "--label", "C", "--at", "220,40", "--rotate", "90", "Capacitance=22.6 pF")
    sim.cli("comp", "add", "Resistor", "--label", "RP", "--at", "280,40", "--rotate", "90", "Resistance=10 MΩ")
    sim.cli("comp", "add", "Oscope", "--label", "SCOPE", "--at", "420,0", "Filter=0.1 V")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "160,140")
    sim.cli("wire", "add", "GEN.outnod", "R1.lPin")
    for p in ("L.lPin", "C.lPin", "RP.lPin", "SCOPE.Pin0"):
        sim.cli("wire", "add", "R1.rPin", p)
    for p in ("L.rPin", "C.rPin", "RP.rPin", "GEN.gndnod", "SCOPE.PinG"):
        sim.cli("wire", "add", p, "G.Gnd")
    sim.cli("settings", "set", "stepSize=1 ns", "reactStep=1 ns", "speed=0.01")
    sim.cli("scope", "set", "SCOPE", "--timediv", "1us", "--trigger", "0", "--triglevel", "auto", "--voltdiv", "0.1")

    # Bursts start every 100 us; the run lengths end 50, 30 and 30 us (5, 3, 3 screens) after one
    for t in ("150us", "230us", "330us"):
        sim.cli("sim", "stop")
        sim.cli("sim", "run", "--for", t)

        data = sim.cli("scope", "data", "SCOPE", "--ch", "0", "--last", "100us")
        v = [s[1] for s in data["channels"][0]["samples"]]
        vpp = (max(v) - min(v)) if v else 0
        sim.log("run %s: scope data %d samples, %.3f V p-p" % (t, len(v), vpp))
        sim.check("run %s: the circuit rings (V p-p)" % t, vpp, 0.8, tol=0.4)

        pts = trace_points(sim)
        ys = [p[1] for p in pts]
        span_y = (max(ys) - min(ys)) if ys else 0
        full = vpp / 0.1 * 14.3          # 143 px high, 10 divisions of 0.1 V
        sim.log("run %s: screen trace %d points, y span %.1f px of %.1f px for the full ring" % (t, len(pts), span_y, full))
        sim.check("run %s: screen shows the burst (fraction of the ring's p-p)" % t, span_y / full if full else 0, 1.0, tol=0.3)

if __name__ == "__main__":
    run_test(test)
