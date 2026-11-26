"""Light input pin on the Photodiode and Phototransistor (Light_Pin, Lux_per_V).

Light was a property or dial only, so a modulated light (Scherz Fig. 5.26 receiver, opto
links, a tachometer) could only be stepped by hand. With Light_Pin on, the part gets a pin
`light` and lux = max(0, V(light)) * Lux_per_V (default 100 lx/V); the dial is then not used.
Expected by hand:
- Photodiode BPW34 (70 nA/lx) shorted by a 1 k resistor, light pin at 5 V: 500 lx,
  35 uA, 35 mV across 1 k. At -1 V: 0 lx (dark).
- 1 kHz square 0/5 V on the pin: the 1 k voltage switches 0 <-> 35 mV at 1 kHz.
- Phototransistor BPW85B (7.3 nA/lx base, gain 500), 10 V through 1 k to the collector,
  emitter grounded, light pin at 2 V: 200 lx, IB 1.46 uA, IC = 0.730 mA, 0.730 V across 1 k.
- Light_Pin off: the dial lux as before (100 lx: 7 uA in the photodiode).
"""

from harness import run_test


def within(sim, name, value, lo, hi):
    sim.check("%s = %.5g in [%g, %g]" % (name, value, lo, hi), int(lo <= value <= hi), 1, absolute=0)


def photodiode(sim, pin, volts):
    c = sim.cli
    c("circuit", "new", "--discard")
    c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0", "Light_Pin=%s" % pin)
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,80", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,160")
    c("wire", "add", "D.anode", "R.lPin")
    c("wire", "add", "D.cathode", "R.rPin")
    c("wire", "add", "D.anode", "G.Gnd")
    if pin == "true":
        c("comp", "add", "Fixed Voltage", "--label", "VL", "--at", "-120,-80", "Voltage=%g" % volts, "Out=true")
        c("wire", "add", "VL.outnod", "D.light")
    c("sim", "run", "--for", "1ms")
    v = c("probe", "D.cathode")[0]["voltage"] - c("probe", "D.anode")[0]["voltage"]
    c("sim", "stop")
    return v


def test(sim):
    c = sim.cli
    try:
        v = photodiode(sim, "true", 5)
    except Exception as e:
        sim.check("photodiode light pin (%s)" % str(e)[-80:], 0, 1, absolute=0)
        return
    within(sim, "photodiode, light pin 5 V: mV across 1 k", -v * 1e3, 34.5, 35.5)
    within(sim, "photodiode, light pin -1 V: mV across 1 k", -photodiode(sim, "true", -1) * 1e3, -0.01, 0.01)
    within(sim, "photodiode, pin off, dial 100 lx: mV across 1 k", -photodiode(sim, "false", 0) * 1e3, 6.9, 7.1)

    # Square-wave light
    c("circuit", "new", "--discard")
    c("comp", "add", "Photodiode", "--label", "D", "--at", "0,0", "Light_Pin=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "0,80", "Resistance=1000")
    c("comp", "add", "Ground", "--label", "G", "--at", "-80,160")
    c("comp", "add", "WaveGen", "--label", "W", "--at", "-200,-80", "Wave_Type=Square", "Freq=1 kHz",
      "Mid_Volt=2.5", "Semi_Ampli=2.5", "Always_On=true")
    c("wire", "add", "D.anode", "R.lPin")
    c("wire", "add", "D.cathode", "R.rPin")
    c("wire", "add", "D.anode", "G.Gnd")
    c("wire", "add", "W.outnod", "D.light")
    c("sim", "run", "--for", "2.25ms")
    a = -c("probe", "D.cathode")[0]["voltage"]
    c("sim", "run", "--for", "0.5ms")
    b = -c("probe", "D.cathode")[0]["voltage"]
    c("sim", "stop")
    within(sim, "1 kHz light: one half-period (mV)", max(a, b) * 1e3, 34.5, 35.5)
    within(sim, "1 kHz light: the other half-period (mV)", min(a, b) * 1e3, -0.01, 0.01)

    # Phototransistor
    c("circuit", "new", "--discard")
    c("comp", "add", "Phototransistor", "--label", "Q", "--at", "0,0", "Light_Pin=true")
    c("comp", "add", "Resistor", "--label", "R", "--at", "80,-80", "Resistance=1000")
    c("comp", "add", "Fixed Voltage", "--label", "VC", "--at", "160,-160", "Voltage=10", "Out=true")
    c("comp", "add", "Fixed Voltage", "--label", "VL", "--at", "-120,-80", "Voltage=2", "Out=true")
    c("comp", "add", "Ground", "--label", "G", "--at", "0,160")
    c("wire", "add", "VC.outnod", "R.rPin")
    c("wire", "add", "R.lPin", "Q.collector")
    c("wire", "add", "Q.emitter", "G.Gnd")
    c("wire", "add", "VL.outnod", "Q.light")
    c("sim", "run", "--for", "1ms")
    vr = 10 - c("probe", "Q.collector")[0]["voltage"]
    c("sim", "stop")
    within(sim, "phototransistor, light pin 2 V: V across 1 k", vr, 0.72, 0.74)


run_test(test)
