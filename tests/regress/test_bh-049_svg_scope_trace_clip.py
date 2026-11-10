"""BH-049: an SVG export draws the oscilloscope trace outside the scope screen.

The screen hides the parts of the trace that run past its edges by painting
margin rectangles over them, and the widget clips the rest. Qt's SVG generator
writes no clipping, so the exported trace keeps segments far outside the screen
(x = -1265 px in a 135 px plot): a viewer that does not clip (macOS QuickLook)
draws a line straight across the circuit.

The same happens at the top and bottom when the signal is larger than the screen.

Right result: every point of the exported trace lies inside the plot widget,
i.e. between its left margin rectangle (x = 0) and the right edge of its right
margin rectangle, and between y = 0 and the margin height.
"""

import os
import re
import tempfile

from harness import run_test

TRACE = "#c8b80a"   # Light theme, channel 1
MARGIN = "#e8e9ee"  # Light theme screen margins (PlotDisplay::paintEvent)


def test(sim):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "WaveGen", "--label", "GEN", "--at", "0,0",
            "Wave_Type=Sine", "Freq=1 kHz", "Semi_Ampli=1 V", "Bipolar=true", "Always_On=true")
    sim.cli("comp", "add", "Oscope", "--label", "SCOPE", "--at", "200,0")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "40,80")
    sim.cli("wire", "add", "GEN.outnod", "SCOPE.Pin0")
    sim.cli("wire", "add", "GEN.gndnod", "G.Gnd")
    sim.cli("wire", "add", "SCOPE.PinG", "G.Gnd")
    sim.cli("scope", "set", "SCOPE", "--timediv", "100us", "--trigger", "none",
            "--voltdiv", "0.05")   # 1 V peak = 20 divisions: runs off the top and bottom
    sim.cli("sim", "speed", "1")         # 5 ms in 0.5 s: the screen updates every frame
    sim.run("5ms")                       # Paused: a full screen of sine
    check_export(sim, "paused")
    sim.cli("sim", "stop")               # Stopped: the last frame, one long flat segment
    check_export(sim, "stopped")


def check_export(sim, state):
    svg = os.path.join(tempfile.mkdtemp(prefix="bh049-"), "scope.svg")
    sim.cli("export", "--file", svg, "--format", "svg", "--theme", "Light", "--meters", "show")
    text = open(svg, encoding="utf-8").read()

    groups = re.findall(r'<g ([^>]*)>(.*?)</g>', text, re.S)

    def attr(a, name):
        m = re.search(name + r'="([^"]*)"', a)
        return m.group(1) if m else None

    width = height = None   # The plot widget, from its margin rectangles
    for a, body in groups:
        if (attr(a, "fill") or "").lower() == MARGIN:
            for x, w, h in re.findall(r'<rect x="([-\d.]+)" y="[-\d.]+" width="([-\d.]+)" height="([-\d.]+)"', body):
                right = float(x) + float(w)
                width = right if width is None else max(width, right)
                height = float(h) if height is None else max(height, float(h))
    sim.log("%s: plot widget %s x %s px" % (state, width, height))
    sim.check("%s: plot margins found in the SVG" % state, int(width is not None), 1, absolute=0)

    pts = []
    for a, body in groups:
        if (attr(a, "stroke") or "").lower() != TRACE:
            continue
        for p in re.findall(r'points="([^"]*)"', body):
            pts += [tuple(map(float, q.split(","))) for q in p.split()]
    sim.log("%s: %d trace points" % (state, len(pts)))
    sim.check("%s: trace exported" % state, int(len(pts) >= 2), 1, absolute=0)
    out = [q for q in pts if q[0] < -1 or q[0] > (width or 0) + 1 or q[1] < -1 or q[1] > (height or 0) + 1]
    if out:
        sim.log("%s: points outside the plot: %s" % (state, out[:5]))
    sim.check("%s: trace points outside the plot" % state, len(out), 0, absolute=0)

if __name__ == "__main__":
    run_test(test)
