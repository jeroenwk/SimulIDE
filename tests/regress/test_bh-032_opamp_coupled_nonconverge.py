"""BH-032: two op-amp stages coupled through diodes never converge (AoE Fig. 4.63 absolute value).

IC1: inverter (R1 = R2 = 10k) with a diode in each feedback path; its R2/R4 node feeds IC2, an
inverting stage (R4 = 5k, R5 = 10k). A textbook circuit: the solver should settle in a few
iterations. Right result: no "Not converging" fault in the event log over 1 ms of a 1 kHz sine
(and the output is |Vin|). Wrong: a fault on every input step ("Still iterating: Da, Db, U1, U2").
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    for args in [
        ("WaveGen", "--label", "W", "--at", "-200,40", "Wave_Type=Sine", "Freq=1000", "Mid_Volt=0", "Semi_Ampli=5", "Always_On=true"),
        ("Resistor", "--label", "R1", "--at", "-120,40", "Resistance=10k"),
        ("OpAmp", "--label", "U1", "--at", "-40,32", "Gain=1000", "Volt_Pos=15", "Volt_Neg=-15"),
        ("Diode", "--label", "Da", "--at", "-40,88", "--rot", "180"),
        ("Diode", "--label", "Db", "--at", "56,-8", "--rot", "180"),
        ("Resistor", "--label", "R2", "--at", "-40,-24", "Resistance=10k"),
        ("Resistor", "--label", "R4", "--at", "120,-8", "Resistance=5k"),
        ("Resistor", "--label", "R5", "--at", "152,-40", "Resistance=10k"),
        ("OpAmp", "--label", "U2", "--at", "280,0", "Gain=1000", "Volt_Pos=15", "Volt_Neg=-15"),
        ("Ground", "--label", "G2", "--at", "-80,72"),
        ("Ground", "--label", "G3", "--at", "232,40"),
    ]:
        c("comp", "add", *args)
    for a in [("W.outnod", "R1.lPin"), ("R1.rPin", "U1.inputInv"), ("U1.inputNinv", "G2.Gnd", "--via", "-80,24"),
              ("U1.output", "Da.lPin", "--via", "-8,32,-8,88"), ("Da.rPin", "U1.inputInv", "--via", "-72,88,-72,40"),
              ("U1.output", "Db.rPin", "--via", "-8,-8"), ("Db.lPin", "R4.lPin", "--via", "96,-8"),
              ("R2.rPin", "Db.lPin", "--via", "-8,-24"), ("R2.lPin", "U1.inputInv", "--via", "-72,-24"),
              ("R4.rPin", "U2.inputInv", "--via", "136,8"), ("R5.lPin", "R4.rPin"),
              ("R5.rPin", "U2.output", "--via", "168,-96,328,-96,328,0"),
              ("U2.inputNinv", "G3.Gnd", "--via", "232,-8,232,40")]:
        c("wire", "add", *a)
    c("sim", "run", "--for", "1ms", timeout=60)
    log = c("log")
    text = str(log)
    n = text.count("Not converging")
    sim.log("log: %s" % text[:600])
    sim.check("'Not converging' faults in 1 ms (got %d)" % n, n, 0, absolute=0.5)


if __name__ == "__main__":
    run_test(test)
