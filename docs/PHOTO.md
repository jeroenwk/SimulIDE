# Photodiode and phototransistor

Two native light sensors (`src/gui/circuitwidget/components/active/photosensor.{h,cpp}`),
with the light level set on the part as on the LDR: a dial (or slider) between `Min_Lux`
and `Max_Lux`, the property `Lux`, or `comp set D1 Lux=500` from the CLI. Or from a
voltage: with `Light_Pin` on, the part has a pin `light` and the light is
max(0, V(light)) × `Lux_per_V` (default 100 lx/V, against ground), so a Wave Generator or
any circuit node can modulate it (a chopped beam, an IR remote, a tachometer disc). The
dial is then hidden and not used; the value label shows the scale.

| Part | Library | Pins | Models |
|---|---|---|---|
| Photodiode | Active / Rectifiers | `anode`, `cathode`, `light` (off by default: `Light_Pin`) | BPW34 (default), Solar cell, Custom |
| Phototransistor | Active / Transistors | `collector`, `emitter`, `base` (off by default: `Base_Pin`), `light` (`Light_Pin`) | BPW85A, BPW85B (default), BPW85C, Custom |

## Model

- **Light.** Lux of illuminant A (an incandescent lamp, the datasheets' "EA = 1 klx"). The
  photocurrent is `Sensitivity × Lux`, linear, with no spectrum, angle or temperature.
- **Photodiode.** A diode (IS, N, breakdown, junction capacitance C(V) = Cj0 / (1 − V/Vj)^M)
  with the photocurrent flowing inside from cathode to anode, and a shunt resistance that
  gives the reverse dark current. Short-circuited it gives the photocurrent; open it rises
  to Voc = N·Vt·ln(I/IS); reverse-biased (photoconductive) it passes the photocurrent plus
  V/Shunt.
- **Phototransistor.** An NPN (the BJT model: gain, IS, BVceo, Cje, Cjc) with the photocurrent
  flowing from collector to base, so IC = (gain + 1)·Iph. Without a base pin (or with it
  left open) the base is an internal node. The speed comes from the junction capacitances
  times the gain, as in a real part: a large load resistor makes it slow.
- Changing a parameter by hand makes the model Custom.
- **Light pin.** A voltage-controlled current: the pin draws no current, and the
  photocurrent follows its voltage at every solve (no lag of its own; the part's speed is
  still its capacitances). `tests/regress/test_photo_light_pin.py`.
- **Solar cells.** `Isc` (short-circuit current at 1 klx) and `Voc` (open-circuit voltage at
  1 klx, without the shunt) set Sensitivity = Isc / 1000 lx and IS = Isc / (e^(Voc/(N·Vt)) − 1),
  Vt = 25.865 mV; setting Isc keeps Voc. Voc rises by N·Vt·ln 10 = 60 mV per decade of light.
  The model "Solar cell" is a small silicon cell (about 60 × 40 mm) [INFERRED, typical]:
  100 mA and 0.60 V in full sun (100 klx, raise `Max_Lux`), Isc 1 mA and Voc 0.481 V at
  1 klx, shunt 1 MΩ, Cj0 10 nF. Put cells in series for a panel (nine for Scherz Fig. 5.21's
  4.5 V); scale Isc with the area. `tests/regress/test_photodiode_solar_cell.py`.

## Fit to the datasheets

`tests/regress/test_photosensor.py` checks:

| | Datasheet | Model [SIMULATED] |
|---|---|---|
| BPW34 Ik, EA = 1 klx | 70 µA typ | 70 µA |
| BPW34 Ira, EA = 1 klx, VR = 5 V | 75 µA typ | 70 µA |
| BPW34 Vo, Ee = 1 mW/cm² | 350 mV typ | 350 mV |
| BPW34 Iro, VR = 10 V, dark | 2 nA typ, 30 nA max | 2.06 nA |
| BPW34 CD at 0 V / 3 V | 70 / 25 pF typ (40 max) | 70 / 25 pF (Vj 0.7 V, M 0.62) |
| BPW85A / B / C Ica, Ee = 1 mW/cm², VCE = 5 V | 0.8–2.5 / 1.5–4 / 3–8 mA | 1.41 / 2.46 / 4.91 mA |
| BPW85 ICEO, VCE = 20 V | 1 nA typ, 200 nA max | 0.05 nA (steady) |
| BPW85 VCEsat, IC = 0.1 mA, Ee = 1 mW/cm² | ≤ 0.3 V | 0.08 V |
| BPW85 ton / toff, 5 V, 5 mA, RL 100 Ω | 2.0 / 2.3 µs typ | 3 / 3 µs |

Sources: Vishay BPW34 (doc. 81521, rev. 2.1, 23-Aug-11), Basic Characteristics p. 2;
Vishay BPW85 (doc. 81531, rev. 2.1, 04-Aug-14), Basic and Type Dedicated Characteristics p. 2.

Where the numbers come from:
- BPW34 Sensitivity 70 nA/lx [DOC: Ik at 1 klx]; IS 6.2e-11 A with N = 1 [INFERRED: gives
  Vo = 350 mV at Ik = 47 µA, the 1 mW/cm² figures]; BV 60 V [DOC: minimum]; Shunt 5 GΩ
  [INFERRED: Iro 2 nA at 10 V]; Cj0 70 pF [DOC], Vj, M [INFERRED: 25 pF at 3 V].
- The BPW85 is specified at Ee = 1 mW/cm², 950 nm, not in lux. The BPW34 gives both
  (Ik 47 µA at 1 mW/cm², 70 µA at 1 klx), so 1 mW/cm² is taken as 671 lx for silicon
  [INFERRED]. Gain 500 [INFERRED: not in the datasheet]; base Sensitivity per class so the
  collector current at 671 lx is the geometric mean of the class's range (A 4.2, B 7.3,
  C 14.6 nA/lx); Cje 60 pF, Cjc 6 pF [INFERRED: fit to ton / toff]; BVceo 70 V [DOC].

What the model leaves out:
- Spectral response (the LED, sunlight or IR are all "lux"), angle, temperature.
- The rise of the reverse light current with bias (BPW34: 75 µA at 5 V against 70 µA
  short-circuited): the model's photocurrent does not depend on the voltage.
- Series resistance of the photodiode (its forward voltage at 50 mA is low).
- The phototransistor's dark current is only the BJT's: 0.05 nA, not the typical 1 nA.
- After power-up a phototransistor with an open base takes some ms in the dark to settle
  (the step charges C_cb through the base, times the gain), as a real one does.

Books: Scherz Figs. 5.18–5.29 (photovoltaic and photoconductive photodiode, phototransistor
switches, light-activated relay, receiver) and 8.41 (light-sensor amplifiers).
