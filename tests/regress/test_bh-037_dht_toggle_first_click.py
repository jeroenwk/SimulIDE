"""BH-037: the first click on a DHT22/DHT11 temperature/humidity toggle does nothing.

Dht22 (micro/dht22.cpp) starts with m_set = true (temperature) but its toggle button starts
unchecked, and onbuttonclicked copies isChecked(): the first click checks it and m_set stays true,
so the next Up still changes the temperature. Right result: click the toggle once, press Up once
and the humidity goes up by HumiInc (68.5 -> 73.5 %), the temperature stays 22.5 degC.
The sensor is read like a microcontroller would: a WaveGen pulls the line low through a diode for
2 ms, then releases it; the scope records the edges, high widths over ~48 us are ones.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def read_bytes(sim, model, clicks):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "DHT22", "--label", "D", "--at", "0,0", "DHT22=" + model)
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-200,-100", "Voltage=5")
    sim.cli("comp", "add", "Resistor", "--label", "R", "--at", "-150,-50", "Resistance=10k")
    sim.cli("comp", "add", "Wave Generator", "--label", "W", "--at", "-200,50", "Wave_Type=Square",
            "Freq=10", "Duty=98", "Semi_Ampli=2.5", "Mid_Volt=2.5", "Steps=1000")
    sim.cli("comp", "add", "Diode", "--label", "DD", "--at", "-100,50")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "-250,100")
    sim.cli("comp", "add", "Oscope", "--label", "SC", "--at", "200,0")
    sim.cli("wire", "add", "V.outnod", "R.lPin")
    sim.cli("wire", "add", "R.rPin", "D.inPin")
    sim.cli("wire", "add", "D.inPin", "DD.lPin")
    sim.cli("wire", "add", "W.outnod", "DD.rPin")
    sim.cli("wire", "add", "R.rPin", "SC.Pin0")
    sim.cli("ui", "set", "V", "on")
    sim.cli("comp", "set", "W", "Always_On=true")
    for c in clicks:
        sim.cli("ui", "click", "D", str(c))        # 0 toggle T/H, 1 up, 2 down
    sim.cli("scope", "set", "SC", "--timediv", "1ms", "--trigger", "none")
    sim.cli("sim", "run", "--for", "250ms", timeout=120)
    smp = sim.cli("scope", "data", "SC", "--max", "100000")["channels"][0]["samples"]
    ev, last = [], None
    for t, v in smp:
        st = v > 2.5
        if last is not None and st != last and t > 0.0995:
            ev.append((t, st))
        last = st
    while ev and ev[0][1]:
        ev.pop(0)
    ev = ev[2:]                                    # acknowledge pulse
    bits = []
    for k in range(0, len(ev) - 2, 2):
        bits.append(1 if ev[k + 2][0] - ev[k + 1][0] > 48e-6 else 0)
        if len(bits) == 40:
            break
    n = 0
    for b in bits:
        n = n * 2 + b
    return [(n >> s) & 0xFF for s in (32, 24, 16, 8, 0)], len(bits)


def test(sim):
    by, nb = read_bytes(sim, "DHT22", [])
    sim.check("DHT22 default: humidity x10", by[0] * 256 + by[1], 685, tol=0)
    by, nb = read_bytes(sim, "DHT22", [0, 1])
    sim.check("DHT22 toggle + up: humidity x10", by[0] * 256 + by[1], 735, tol=0)
    sim.check("DHT22 toggle + up: temperature x10", by[2] * 256 + by[3], 225, tol=0)


run_test(test)
