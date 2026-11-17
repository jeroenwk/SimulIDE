"""Supply ramp: DC sources rise from 0 at the start of a run (create-circuit sim-coldstart, #343, #363).

Every DC source stepped to its full value at t = 0. Some loops then settled in a state a
real supply, which ramps from 0, never visits:
- #343, Scherz fig 8.40 precision current sink (test_cc-coldstart_sink.sim1, saved with
  start-up noise as the workaround): without noise the op amp railed at +12 V and the
  load carried 0.12 mA instead of VIN / R2 = 1 V / 1 kOhm = 1.00 mA.
- #362 / #363, AoE fig 3.27 JFET cascode sink (test_cc-coldstart_cascode.sim1): at RL 2.2 kOhm
  a fresh start landed on 1.12 mA; the sink sets ~2.6 mA (Q1 2N5484 with RS 220 Ohm, the
  value the same circuit gives at RL 1 kOhm and when reached by continuation).

The setting `supplyRamp` (0 = a step, as before) makes Battery, Rail and Fixed Voltage
rise linearly in 50 steps over that time; the property keeps its value. Right results:
- a 10 V battery / rail / fixed voltage into 1 kOhm with supplyRamp 1 ms: ~5 mA at
  0.5 ms (half way), 10 mA after the ramp; the setting survives save + reopen;
- fig 8.40 with noise off and supplyRamp 1 ms: 1.00 mA, op amp not railed;
- fig 3.27 at RL 2.2 kOhm with supplyRamp 1 ms: the same current as at RL 1 kOhm.
"""
import os, tempfile
from harness import run_test

HERE = os.path.dirname(os.path.abspath(__file__))


def test(sim):
    c = sim.cli
    for typ, pin, gnd in (("Battery", "lPin", "rPin"), ("Rail", "outnod", None), ("Fixed Voltage", "outnod", None)):
        c("circuit", "new", "--discard")
        c("settings", "set", "supplyRamp", "1ms")
        c("comp", "add", typ, "--label", "S", "--at", "-120,0", "Voltage=10")
        c("comp", "add", "Resistor", "--label", "R", "--at", "0,-80", "Resistance=1k")
        c("comp", "add", "Ground", "--label", "G", "--at", "0,120")
        c("wire", "add", "S." + pin, "R.lPin")
        c("wire", "add", "R.rPin", "G.Gnd")
        if gnd:
            c("wire", "add", "S." + gnd, "G.Gnd")
        if typ == "Fixed Voltage":
            c("comp", "set", "S", "Out=true")
        sim.run("510us")
        half = abs(sim.read("R")["current"])
        sim.check("%s: half way up the 1 ms ramp (A)" % typ, half, 0.0051, tol=0.05)
        sim.run("1.5ms")
        full = abs(sim.read("R")["current"])
        sim.check("%s: full value after the ramp (A)" % typ, full, 0.010, tol=0.001)

    path = os.path.join(tempfile.mkdtemp(), "ramp.sim1")
    c("circuit", "save", path)
    c("circuit", "open", path, "--discard")
    sim.check("supplyRamp saved with the circuit", int("1 ms" in str(c("settings", "get", "supplyRamp"))), 1, absolute=0)

    # #343: the current sink, start-up noise off
    c("circuit", "open", os.path.join(HERE, "test_cc-coldstart_sink.sim1"), "--discard")
    c("settings", "set", "noise", "false")
    c("settings", "set", "supplyRamp", "1ms")
    sim.run("10ms")
    out = c("read", "OA")["pins"]["output"]
    i = abs(sim.read("Load")["current"])
    sim.check("#343 sink: load current VIN/R2 (A), op amp at %.3g V" % out, i, 0.001, tol=0.02)
    sim.check("#343 sink: op amp not railed", int(-11 < out < 11), 1, absolute=0)

    # #363: the cascode at a load that started dead
    c("circuit", "open", os.path.join(HERE, "test_cc-coldstart_cascode.sim1"), "--discard")
    c("settings", "set", "supplyRamp", "1ms")
    sim.run("5ms")
    ref = abs(sim.read("RL")["current"])
    c("comp", "set", "RL", "Resistance=2.2k")
    c("sim", "stop")
    sim.run("5ms")
    i = abs(sim.read("RL")["current"])
    sim.check("#363 cascode: RL 2.2k fresh start = the RL 1k current %.4g A" % ref, i, ref, tol=0.02)
    sim.check("#363 cascode: about 2.6 mA (A)", ref, 0.0026, tol=0.05)

    # #362: the same cascode with the printed J111 as Q2: a step start put Q2's source at
    # 19.6 V on the 15 V rail and RL at 0 A; ramped, it sinks what Q1 sets (~2.6 mA)
    c("circuit", "open", os.path.join(HERE, "test_cc-coldstart_cascode.sim1"), "--discard")
    c("settings", "set", "supplyRamp", "1ms")
    c("comp", "set", "Q2", "Model=J111")
    sim.run("10ms")
    q2 = c("read", "Q2")
    i = abs(sim.read("RL")["current"])
    sim.check("#362 J111 cascode: Q2 source inside the 15 V rail (%.3g V)" % q2["pins"]["Sour"],
              int(0 <= q2["pins"]["Sour"] <= 15), 1, absolute=0)
    sim.check("#362 J111 cascode: RL current set by Q1 (A)", i, 0.0026, tol=0.05)


if __name__ == "__main__":
    run_test(test)
