"""BH-051: after `sim run --for` the oscilloscope shows an old (or empty) screen.

The scope copies new samples to its screen once per GUI frame (50 ms), and only
while the simulation is not paused. A run shorter than a frame, like
`sim run --for 2ms` at 100 % speed, ends paused before any frame: the screen
stays empty, although `scope data` has the samples. In the GUI the same leaves
the screen up to one frame behind after pressing pause.

Right result: once paused, the screen shows the data up to the pause time. Here a
1 kHz sine at 0.5 ms/div: the 2 ms of data cover 2 of the 5 ms on screen (x from
60 % to the right edge), and the trace swings over the samples' peak-to-peak
range at 0.5 V/div (10 divisions over the plot's height).
"""

import os
import re
import tempfile

from harness import run_test

TRACE = "#c8b80a"   # Light theme, channel 1


def trace_points(sim):
    svg = os.path.join(tempfile.mkdtemp(prefix="bh051-"), "scope.svg")
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
            "Wave_Type=Sine", "Freq=1 kHz", "Semi_Ampli=1 V", "Bipolar=true", "Always_On=true")
    sim.cli("comp", "add", "Oscope", "--label", "SCOPE", "--at", "200,0")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "40,80")
    sim.cli("wire", "add", "GEN.outnod", "SCOPE.Pin0")
    sim.cli("wire", "add", "GEN.gndnod", "G.Gnd")
    sim.cli("wire", "add", "SCOPE.PinG", "G.Gnd")
    sim.cli("scope", "set", "SCOPE", "--timediv", "500us", "--trigger", "none", "--voltdiv", "0.5")
    sim.cli("settings", "set", "speed", "100")
    sim.run("2ms")                       # Under one 50 ms frame of wall time

    data = sim.cli("scope", "data", "SCOPE", "--ch", "0", "--last", "2ms")
    samples = data["channels"][0]["samples"]
    n = len(samples)
    vpp = (max(v for t, v in samples) - min(v for t, v in samples)) if samples else 0
    sim.log("scope data: %d samples, %.3f V p-p" % (n, vpp))
    sim.check("scope has the samples", int(n > 100), 1, absolute=0)

    pts = trace_points(sim)
    xs = [p[0] for p in pts]
    ys = [p[1] for p in pts]
    span_x = (max(xs) - min(xs)) if xs else 0
    span_y = (max(ys) - min(ys)) if ys else 0
    sim.log("screen trace: %d points, x span %.1f px, y span %.1f px" % (len(pts), span_x, span_y))
    # Plot 135 px wide, 10 divisions: 2 ms of 5 ms = 54 px; 143 px high, 10 divisions of 0.5 V
    sim.check("trace covers the 2 ms run (px)", span_x, 54, tol=0.25)
    sim.check("trace swings over the samples' p-p (px)", span_y, vpp / 0.5 * 14.3, tol=0.25)


if __name__ == "__main__":
    run_test(test)
