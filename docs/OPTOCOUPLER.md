# Optocouplers

Three native parts (`src/gui/circuitwidget/components/active/optocoupler.{h,cpp}`), in
Active / Other Active. Each has an infrared LED (`anode`, `cathode`) whose forward current
IF drives an output side with no electrical connection to it.

| Part | Output pins | Models | Output model |
|---|---|---|---|
| Optocoupler | `collector`, `emitter`, `base` (off by default: `Base_Pin`) | 4N35 (default), Custom | NPN with a base photocurrent IF·CTR/gain, so IC = CTR·IF in the active region |
| Opto-Triac | `mt2`, `mt1` | MOC3021 (random phase, default), MOC3041 (zero-cross), Custom | Triac switch: fires while IF > IFT (zero-cross types only while \|V(MT2−MT1)\| < Vinh), stays on while the LED is on or the main current is above IH |
| Opto Logic | `vcc`, `enable`, `out`, `gnd` | 6N137 (default), Custom | Open-collector output pulled low (Ron) while IF > ITH, the enable is above 1.4 V and VCC above 2.5 V, after tPHL / tPLH; supply current ICC; enable pull-up to VCC |

- The LED is a diode with IS fitted to the typical VF at the datasheet's test current, N = 1.8.
- The opto-triac decides on each step's converged values and switches 1 ps later: the LED
  current read during the iterations can be far off.
- CLI `read`: `i_led`; `i_photo`, `i_collector`; `on`, `i_main`; `out_low`.
- Changing a parameter by hand makes the model Custom.

## Fit to the datasheets

`tests/regress/test_optocoupler.py` (24 checks):

| | Datasheet | Model [SIMULATED] |
|---|---|---|
| 4N35 VF, IF = 10 mA | 1.3 V typ, 1.5 max | 1.30 V |
| 4N35 CTR, IF = 10 mA, VCE = 10 V | ≥ 100 % | 152 % |
| 4N35 ICEO, VCE = 10 V | 5 nA typ, 50 max | 8.5 nA (after 200 ms: the open base settles slowly) |
| 4N35 ton / toff, 10 V, IC 2 mA, RL 100 Ω | 10 / 10 µs typ | 10 / 11 µs |
| MOC3021 fires at IF = IFT max (15 mA), 3 V on MT | yes | yes; not at 2 mA |
| MOC3021 VTM, 100 mA | 1.8 V typ, 3 max | 1.79 V |
| MOC3021 on 50 Hz, 100 V peak, 1 k | conducts both halves; LED off: drops out at the next zero crossing | yes |
| MOC3041 LED on at the peak | waits for the next zero crossing (VIH ≤ 20 V) | yes |
| 6N137 VOL, IOL 13 mA | 0.2 V typ, 0.6 max | 0.21 V |
| 6N137 ITH | 2.4 mA typ, 5 max | low at 5 mA, high at 1 mA |
| 6N137 enable low | output high | high |
| 6N137 ICC, IF = 0, VE = VCC | 3.3 mA typ | 3.3 mA |
| 6N137 tPHL / tPLH | 50 / 48 ns typ, 75 max | 50 / 50 ns (sampled every 5 ns) |

Sources: Vishay 4N35/4N36/4N37 (doc. 81181, rev. 1.2, 07-Jan-10), Electrical, CTR and
Switching Characteristics p. 154–155; onsemi MOC3023M/D (rev. 3, June 2023) and
MOC3043M/D (rev. 4, Sept. 2022), Electrical Characteristics p. 3; Vishay 6N137
(doc. 84732, rev. 2.2, 15-Mar-2023), p. 3–4.

[INFERRED] values:
- 4N35: CTR 150 % (the datasheet gives only ≥ 100 %); transistor gain 500; Cje 100 pF,
  Cjc 20 pF (fit to ton / toff); BVceo 30 V is [DOC].
- MOC3021 / MOC3041: IFT 8 mA (the datasheets give only the 15 mA maximum); Ron 11 Ω (fit to
  VTM 1.8 V at 100 mA with the triac's diodes); off-state 10 GΩ (IDRM 10 nA typ at rated
  voltage); inhibit voltage 10 V (20 V max). IH 100 µA / 400 µA are [DOC] (typical).
- 6N137: Ron 15 Ω (VOL 0.2 V at 13 mA); enable pull-up 6 kΩ (IEH −0.6 mA at 2 V, IEL
  −0.8 mA at 0.5 V); output off 1 GΩ.

What the models leave out:
- CTR's dependence on IF and temperature (it is constant), LED and phototransistor ageing.
- The opto-triac's dv/dt rating, the zero-cross type's leakage in the inhibited state,
  and its VTM curve (a diode plus Ron).
- The 6N137's output stage (a Schottky-clamped transistor: Ron only), its common-mode
  transient immunity, and input hysteresis.
- Isolation: the two sides share nothing in the model, not even the 0.6 pF coupling.

Books: AoE Figs. 12.86–12.94 (logic, gate-driver, analog, dc and ac solid-state-relay
optocouplers, opto-triac triggering a triac); Scherz Figs. 5.30–5.33 (isolators, level
shifters, solid-state relay). A MOSFET-output (PhotoMOS) solid-state relay is not a model
yet: build it from an Opto Logic or Optocoupler driving a MOSFET.
