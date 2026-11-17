"""An SCR whose cathode is opened turns off, even with its gate held low (create-circuit #370).

The latch condition counted the anode current toward holding wherever it left the part:
-Ik/It + Ia*(1/Ih - 1/It) > 1. With the reset switch in the cathode opened and the gate
held at 0 V by the comparator output (Scherz fig 8.42 crowbar), the anode current
(~52 mA) left through the gate resistance, the cathode carried 0 A, and the term
Ia*(1/Ih - 1/It) alone kept the SCR ON; closing the switch re-latched the crowbar with no
trigger. A real SCR regenerates only through its cathode junction: with no cathode current
it drops out (below the holding current, HoldCurr 8.2 mA) and stays off until fired again.

Circuit (the fig 8.42 crowbar, reduced): 6 V / 2 Ohm source -> SCR anode (RL 600 Ohm load
in parallel), cathode -> reset switch SK -> ground; the gate driven through 1 Ohm by a
source VG standing in for the comparator's stiff output: 0 V, or 3 V to fire it
((3 V - V_GK) / 100 Ohm GateRes > TrigCurr 10 mA). With the cathode open the anode current
leaves through the gate: (6 V - ~0.7 V) / (2 + 100 + 1) Ohm = ~52 mA, enough for the old
term (52 mA * (1/8.2 mA - 1/10 mA) = 1.14 > 1) to keep it ON.
1. VG 3 V, then 0 V: latched, (6 V - ~0.9 V) / 2 Ohm = ~2.5 A.
2. SK opened (reset): the SCR must report OFF.
3. SK closed again, no gate drive: OFF, the load back at ~6 V * 600/602.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Battery", "--label", "B", "--at", "-240,0", "Voltage=6", "Resistance=2")
    c("comp", "add", "Battery", "--label", "VG", "--at", "-240,200", "Voltage=0")
    c("comp", "add", "Resistor", "--label", "RL", "--at", "-80,0", "--rotate", "90", "Resistance=600")
    c("comp", "add", "SCR", "--label", "Q", "--at", "0,0")
    c("comp", "add", "Switch", "--label", "SK", "--at", "80,80")
    c("comp", "add", "Resistor", "--label", "R3", "--at", "60,160", "Resistance=1")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,280")
    c("wire", "add", "B.lPin", "Q.lPin")
    c("wire", "add", "B.lPin", "RL.lPin")
    c("wire", "add", "RL.rPin", "G.Gnd")
    c("wire", "add", "B.rPin", "G.Gnd")
    c("wire", "add", "Q.rPin", "SK.pinP0")
    c("wire", "add", "SK.switch0pinN", "G.Gnd")
    c("wire", "add", "Q.gPin", "R3.lPin")
    c("wire", "add", "R3.rPin", "VG.lPin")
    c("wire", "add", "VG.rPin", "G.Gnd")
    c("ui", "set", "SK", "on")          # reset switch closed
    sim.run("5ms")
    st = sim.read("Q").get("state")
    sim.check("not fired yet: OFF (%s)" % st, int(st == "OFF"), 1, absolute=0)

    c("comp", "set", "VG", "Voltage=3"); sim.run("5ms")
    c("comp", "set", "VG", "Voltage=0"); sim.run("5ms")
    st, v = sim.read("Q").get("state"), abs(sim.read("RL")["voltage"])
    sim.check("fired, gate released: latched ON (%s, load %.3g V)" % (st, v), int(st == "ON" and v < 1.5), 1, absolute=0)

    c("ui", "set", "SK", "off"); sim.run("5ms")
    st = sim.read("Q").get("state")
    sim.check("reset switch open: OFF (%s)" % st, int(st == "OFF"), 1, absolute=0)

    c("ui", "set", "SK", "on"); sim.run("5ms")
    st, v = sim.read("Q").get("state"), abs(sim.read("RL")["voltage"])
    sim.check("reset switch closed again, no gate: OFF (%s)" % st, int(st == "OFF"), 1, absolute=0)
    sim.check("load back at 6 V * 600/602 (V)", v, 6 * 600 / 602, tol=0.01)
    sim.finite("SCR", "Q")


if __name__ == "__main__":
    run_test(test)
