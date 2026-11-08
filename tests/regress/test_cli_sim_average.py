"""CLI `sim average`: time-weighted mean / RMS / min / max over every converged solver step.

A WaveGen square wave, 0..5 V (Mid_Volt 2.5, Semi_Ampli 2.5), 1 kHz, Duty 25 %, into 1 kOhm:
mean = 5 V x 0.25 = 1.25 V, RMS = 5 V x sqrt(0.25) = 2.5 V, min 0, max 5; resistor current
mean 1.25 mA. Over 20 whole periods (20 ms). A sine of 2.5 V amplitude around 0 V has mean 0
and RMS 2.5/sqrt(2) = 1.768 V (the generator's 100 steps per period keep it within 1 %).
Scope samples sit on a fixed grid and gave means 10 % off on a buck converter (IMP-016).
"""

from harness import run_test


def build(sim, wave, duty="50"):
    sim.cli("circuit", "new", "--discard")
    sim.cli("comp", "add", "WaveGen", "--label", "W", "--at", "0,0", "Wave_Type=%s" % wave,
            "Freq=1kHz", "Duty=%s" % duty, "Always_On=true", "Semi_Ampli=2.5V", "Mid_Volt=%s" % ("2.5V" if wave == "Square" else "0V"))
    sim.cli("comp", "add", "Resistor", "--label", "R1", "--at", "100,0", "Resistance=1k")
    sim.cli("comp", "add", "Ground", "--label", "G", "--at", "100,100")
    sim.cli("wire", "add", "W.outnod", "R1.lPin")
    sim.cli("wire", "add", "R1.rPin", "G.Gnd")


def test(sim):
    build(sim, "Square", "25")
    r = sim.cli("sim", "average", "--for", "20ms", "W.outnod", "R1")
    pin, res = r["channels"]
    sim.log("average: %s" % r)
    sim.check("square 25 %: mean voltage", pin["voltage"]["mean"], 1.25, tol=0.01)
    sim.check("square 25 %: RMS voltage", pin["voltage"]["rms"], 2.5, tol=0.01)
    sim.check("square 25 %: max voltage", pin["voltage"]["max"], 5.0, tol=0.001)
    sim.check("square 25 %: min voltage", pin["voltage"]["min"], 0.0, absolute=1e-6)
    sim.check("square 25 %: mean resistor current", res["current"]["mean"], 1.25e-3, tol=0.01)
    sim.check("span 20 ms", int(r["span_ps"]) * 1e-12, 20e-3, tol=1e-9)
    # a square wave changes only at its edges: 2 per period, 40 in 20 ms (plus the start)
    sim.check("settled steps at least the 40 edges", min(r["steps"], 40), 40, absolute=0.5)

    build(sim, "Sine")
    r = sim.cli("sim", "average", "--for", "20ms", "W.outnod")
    v = r["channels"][0]["voltage"]
    sim.check("sine: mean voltage", v["mean"], 0.0, absolute=0.01)
    sim.check("sine: RMS voltage", v["rms"], 2.5 / 2 ** 0.5, tol=0.01)


if __name__ == "__main__":
    run_test(test)
