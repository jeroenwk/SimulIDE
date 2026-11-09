"""BH-036: a DHT11 below 0 degC sends garbage.

Dht22::calcData (micro/dht22.cpp), DHT11 branch: tempI = m_temp converts a negative double to
unsigned (undefined) and the sign `1<<8` is ORed into the decimal byte, so after the <<8 it lands in
the integer byte. The ASAIR DHT11 sends the integer part (2) and the decimal part (5) with bit 7 set:
bytes 0x02 0x85 for -2.5 degC, with a valid checksum.
The sensor is read like a microcontroller would: a WaveGen pulls the line low through a diode for
20 ms, then releases it; the scope records the edges, high widths over ~48 us are ones.
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def read_bytes(sim, model, downs):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "DHT22", "--label", "D", "--at", "0,0", "DHT22=" + model)
    sim.cli("comp", "add", "Fixed Voltage", "--label", "V", "--at", "-200,-100", "Voltage=5")
    sim.cli("comp", "add", "Resistor", "--label", "R", "--at", "-150,-50", "Resistance=10k")
    sim.cli("comp", "add", "Wave Generator", "--label", "W", "--at", "-200,50", "Wave_Type=Square",
            "Freq=5", "Duty=90", "Semi_Ampli=2.5", "Mid_Volt=2.5", "Steps=1000")
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
    for _ in range(downs):
        sim.cli("ui", "click", "D", "2")           # temperature down by TempInc (0.5)
    sim.cli("scope", "set", "SC", "--timediv", "1ms", "--trigger", "none")
    sim.cli("sim", "run", "--for", "450ms", timeout=120)
    smp = sim.cli("scope", "data", "SC", "--max", "100000")["channels"][0]["samples"]
    ev, last = [], None
    for t, v in smp:
        st = v > 2.5
        if last is not None and st != last and t > 0.1995:
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
    by, nb = read_bytes(sim, "DHT11", 0)
    sim.check("DHT11 22.5 degC: bits read", nb, 40, tol=0)
    sim.check("DHT11 22.5 degC: temperature integer byte", by[2], 22, tol=0)
    by, nb = read_bytes(sim, "DHT11", 50)           # 22.5 - 25 = -2.5 degC
    sim.check("DHT11 -2.5 degC: temperature integer byte", by[2], 2, tol=0)
    sim.check("DHT11 -2.5 degC: decimal byte (sign in bit 7)", by[3], 0x85, tol=0)
    sim.check("DHT11 -2.5 degC: checksum", by[4], (by[0] + by[1] + by[2] + by[3]) & 0xFF, tol=0)


run_test(test)
