# PUT and UJT

Two native parts (`src/gui/circuitwidget/components/active/unijunction.{h,cpp}`), with
a Model list like the diodes (`resources/data/unijunctions.model`; more models can be
added there):

| Part | Library | Pins | Models |
|---|---|---|---|
| PUT (programmable unijunction transistor) | Active / Rectifiers | `anode`, `cathode`, `gate` | 2N6027 (default), 2N6028, Custom |
| UJT (unijunction transistor) | Active / Transistors | `emitter`, `base1`, `base2` | 2N2646 (default), Custom |

## Model

Both are the two-transistor latch drawn in the datasheets (onsemi 2N6027/D Fig. 8):

```
anode ──E PNP B── gate ──C NPN E── cathode
            C                B
            └──── mid ───────┘
                   └── Rsh ── cathode
```

- The PNP fires when the anode rises a diode drop above the gate; its collector current
  turns the NPN on, which pulls the gate down: the loop latches.
- `Shunt` (Rsh, NPN base to cathode) keeps the NPN off at very small currents.
- `GainN` (the NPN's current gain) sets the valley current: holding needs the NPN to sink
  what the gate's source supplies, (VS / RG) / GainN.
- The UJT is the same latch with its divider inside: RBB from base 2 to base 1, tapped
  at `Eta` (RB2 = RBB (1 - eta) to the inner gate, RB1 = RBB eta to base 1). Its emitter
  is the anode, base 1 the cathode.
- Capacitance group: the three junctions J1 (anode-gate / emitter), J2 (centre), J3
  (gate-cathode / base 1), 0 = none; while noise is on the BJT's typical values
  (4.5, 3.6, 4.5 pF) [INFERRED: neither datasheet gives them].

Changing any parameter by hand makes the model Custom; a circuit loaded with the
model's own values keeps the model.

## Fit to the datasheets

`tests/regress/test_unijunction.py` checks every guaranteed limit (min / max) of the
electrical characteristics, by tracing V(I) with a current source:

| | Datasheet (guaranteed) | Model |
|---|---|---|
| 2N6027 IP, RG 1 M / 10 k | ≤ 2 / ≤ 5 µA | 1.9 / 2.5 µA |
| 2N6027 VT, RG 1 M / 10 k | 0.2–1.6 / 0.2–0.6 V | 0.45 / 0.44 V |
| 2N6027 IV, RG 1 M / 10 k / 200 Ω | ≤ 50 µA / ≥ 70 µA / ≥ 1.5 mA | 8.8 µA / 0.33 mA / 17 mA |
| 2N6027 VF at 50 mA | ≤ 1.5 V | 0.73 V |
| 2N6028 IP, RG 1 M / 10 k | ≤ 0.15 / ≤ 1.0 µA | 0.09 / 0.27 µA |
| 2N6028 IV, RG 1 M / 10 k / 200 Ω | ≤ 25 µA / ≥ 25 µA / ≥ 1.0 mA | 3.8 µA / 0.33 mA / 17 mA |
| 2N2646 RBB | 4.7–9.1 kΩ | 7 kΩ |
| 2N2646 VP at 10 V (eta 0.56–0.75) | 6.0–8.2 V | 6.9 V |
| 2N2646 IV at 20 V | ≥ 4 mA | 4.7 mA |
| 2N2646 IP at 25 V | ≤ 5 µA | 3.8 µA |
| 2N2646 VE(sat) at 50 mA | ≤ 2.5 V | 0.73 V |

Sources: onsemi 2N6027/D rev. 6 (2006), Electrical Characteristics p. 3 and Figs. 4–6;
Comset 2N2646/2N2647 datasheet, Electrical Characteristics p. 2.

What the model leaves out:
- The typical valley currents of the 2N6027 (18 µA at 1 M, 150 µA at 10 k, Fig. 4) are off
  by about 2× in opposite directions (8.8 µA, 0.33 mA): in the latch the valley current
  goes as 1/RG, the real one more slowly. Only the guaranteed limits are met.
- The UJT's conductivity modulation: VE(sat) is a diode drop plus the NPN's saturation
  (0.73 V at 50 mA, real parts 1.5–2.5 V), and the base-2 current after firing is
  VBB / RB2 (4 mA at 10 V; the 2N2646's IB2(mod) is 15 mA typical).
- No 2N2647: its IP ≤ 2 µA together with IV ≥ 8 mA asks for IV / IP ≥ 4000, and in the
  latch the ratio is about eta·VBB / Vt (about 600 at 20 V), whatever the gains.
- Temperature, and the slow recovery after the on-state.

## Oscillators

The test also runs the books' relaxation oscillators: a UJT with 47 k and 100 nF at 12 V
(193 Hz by the formula with VP = 0.65·12 + 0.45 V and VV = 0.7 V; the model gives 193 Hz)
and a PUT on a 10 k / 10 k gate divider with 100 k and 100 nF (141 Hz; model 139 Hz).
