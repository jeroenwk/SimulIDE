"""`read` of a Phototransistor or Photodiode with Light_Pin on reports the lux in use, the
light pin's (create-circuit #772, #763).

It printed the hidden dial's Lux (100) in every state, while i_photo followed the pin.
Expected by hand: light pin at 2 V, Lux_per_V 100: 200 lx; at 0 V: 0 lx; pin off: the dial's 100.
"""

from harness import run_test


def test(sim):
    c = sim.cli
    for typ in ("Phototransistor", "Photodiode"):
        for volts in (2.0, 0.0):
            c("circuit", "new", "--discard")
            c("comp", "add", typ, "--label", "Q", "--at", "0,0", "Light_Pin=true")
            c("comp", "add", "Fixed Voltage", "--label", "VL", "--at", "-120,-80", "Voltage=%g" % volts, "Out=true")
            c("wire", "add", "VL.outnod", "Q.light")
            c("sim", "run", "--for", "1ms")
            r = c("read", "Q")
            r = r[0] if isinstance(r, list) else r
            sim.check("%s light pin %g V: lux" % (typ, volts), r["lux"], volts * 100, tol=0.01, absolute=0.01)
            c("sim", "stop")
    c("circuit", "new", "--discard")
    c("comp", "add", "Phototransistor", "--label", "Q", "--at", "0,0")
    c("sim", "run", "--for", "1ms")
    r = c("read", "Q")
    r = r[0] if isinstance(r, list) else r
    sim.check("pin off: the dial's lux", r["lux"], 100, tol=0.01)


run_test(test)
