"""LDR resistance follows R1*Lux^-Gamma down to millilux (create-circuit #540, guard).

A build read 5.0 MOhm at Lux 0.0062 where the formula gives 10 MOhm, and quoted measured node
voltages instead. Measured alone (1 V rail, LDR to ground, R = V/I) the part follows the
formula at every level, so the 5 MOhm came from something in parallel in that circuit
(a 10 MOhm meter or bias path), not from the model. Expected, R1 127410, Gamma 0.8582:
1000 lux 339.3 ohm, 19.4 lux 10.0 k, 1 lux 127.4 k, 0.1 lux 919 k, 0.0062 lux 9.99 M.
"""
from harness import run_test


def test(sim):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Rail", "--label", "V", "--at", "0,0", "Voltage=1")
    c("comp", "add", "LDR", "--label", "L", "--at", "80,0")
    c("comp", "add", "Ground", "--label", "G", "--at", "160,40")
    c("wire", "add", "V.outnod", "L.lPin")
    c("wire", "add", "L.rPin", "G.Gnd")
    for lux in (1000, 19.4, 1, 0.1, 0.0062):
        c("comp", "set", "L", "Lux=%g" % lux)
        c("sim", "run", "--for", "1ms")
        r = 1 / abs(c("probe", "L.lPin")[0]["current"])
        sim.check("R at %g lux (ohm)" % lux, r, 127410 * lux ** -0.8582, tol=0.005)
    c("sim", "stop")


run_test(test)
