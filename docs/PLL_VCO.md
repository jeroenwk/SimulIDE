# PLL 4046 and VCO 566

Two native parts (`src/gui/circuitwidget/components/active/pllvco.{h,cpp}`), Active / Other
Active, drawn as boxes with named pins. Each pin's current (`comp get`) includes what the
part's internal elements draw there (create-circuit #655). Both are relaxation oscillators built the way the
chips are: the timing current really flows through the external R1 (and R2) and charges
the external timing capacitor between two thresholds, so the parts drawn in a book's
circuit set the frequency. Logic and switching decisions are taken on each step's
converged values and applied 1 ps later.

**The frequency does not depend on `reactStep`** (create-circuit #653, #660, #738). The timing
capacitor is a library capacitor that commits its charge only at its own steps (between them its
node shows where the step will end). Each half period ends on the last committed voltage, and
the voltage it fell short of (or passed) its threshold by is carried into the next threshold, so
the mean frequency is exact at any step (4046 at 10 kHz: 10008 Hz at 1 µs, it read 10417) and
each edge sits within one step of its ideal time. A coarse step still shows on the waveform
(the ramps are steps of I·dt/C) and on the edge timing: keep `reactStep` well below the period
when the edges themselves matter.

## PLL 4046 (CD4046B)

| Pin (number) | Model |
|---|---|
| SIG IN (14), COMP IN (3) | Logic inputs at VDD/2 with 5 % hysteresis; SIG IN self-biased to VDD/2 through 1 MΩ [DOC R14], so it can be AC coupled. A signal must cross VDD/2 ± 5 %: a 566's square (V+/2 to 0.95 V+) DC-coupled into SIG never goes low, so couple it through a capacitor as the datasheet does (create-circuit #610) |
| VCO IN (9) | High impedance. Pin 11 is held at it, so I1 = VCO IN / R1 |
| R1 (11), R2 (12) | Held at VCO IN and at VDD (1 Ω): I = VCOin/R1 + VDD/R2 |
| C1A (6), C1B (7) | I charges C1 into one side while the other is held at VSS (10 Ω); reversed when the charged side is 2.5 V above the other: f = I / (10 V · C1), so fmax = 1/(R1 C1) at VCO IN = 10 V, fmin = 1/(R2 C1) |
| VCO OUT (4) | High while pin 6 charges (50 % duty) |
| PC1 OUT (2) | Exclusive-OR of SIG and COMP |
| PC2 OUT (13) | Edge-triggered phase-frequency detector: VDD after a SIG rising edge, VSS after a COMP rising edge, three-state when both or neither (both reset together) |
| PCP OUT (1) | Phase pulses: high while PC2 is three-state (locked), low during its pulses |
| DEM OUT (10) | Source follower: VCO IN − 1.8 V [DOC], 200 Ω; needs a load (RS ≥ 10 kΩ, as the datasheet) |
| INH (5) | High: VCO stopped (output low), R1 / R2 / DEM OUT released |
| ZENER (15) | 5.5 V zener to VSS [DOC] |
| VDD (16), VSS (8) | Every output is relative to VSS; output resistance 1900 Ω / VDD [INFERRED from IOL] |

The frequency law is calibrated at VDD = 10 V (fmax 1.2 MHz typ for R1 10 kΩ, C1 50 pF +
32 pF internal [DOC p. 3-127]); the chip's fmax at 5 V and 15 V (0.6, 1.6 MHz) follows
VCO IN up to VDD in the model (0.61, 1.8 MHz). Left out: the internal 32 pF, propagation
delays (100–350 ns), the input amplifier's sensitivity, supply current, temperature.

## VCO 566 (NE566)

| Pin | Model |
|---|---|
| VC (5) | Control voltage, 1 MΩ to GND [DOC] (`comp get` gives its current); keep it between ¾ V+ and V+: the state (Show State, CLI `read`) is RUNNING there, VC LOW below (the model keeps the linear law, the real part does not), STOPPED at V+ |
| R1 (6) | Held at VC: I = (V+ − VC) / R1 through R1 from V+ |
| C1 (7) | Charged by +I and discharged by −I between 3/8 and 5/8 of the supply: f0 = 2 (V+ − VC) / (R1 C1 V+) [DOC p. 401] |
| TRI (4) | Buffered triangle, 50 Ω [DOC]: 2.4 Vpp at 12 V |
| SQR (3) | Square, 50 Ω [DOC], 5.4 Vpp at 12 V, from V+/2 to 0.95 V+ [INFERRED levels] |
| V+ (8), GND (1) | Supply current 7 mA at 12 V [DOC], as a resistor |

## Checks

`tests/regress/test_pll_vco.py` (15 checks, [SIMULATED]):

| | Expected | Model |
|---|---|---|
| 4046 VCO, R1 10 k, C1 1 nF, VCO IN 10 / 5 V | 100 / 50 kHz | 100.0 / 50.0 kHz |
| … with R2 100 k, VCO IN 5 V | 60 kHz | 60.1 kHz |
| Inhibit | no output | 0 Hz |
| DEM OUT at VCO IN 5 V | 3.2 V | 3.19 V |
| Zener, ≈ 50 µA | 4.45–6.15 V | 5.38 V |
| PLL with PC2 (10 k, 1 k + 100 nF), 30 kHz in | lock, VCO IN 3 V, phase pulses high | 30.00 kHz, 3.00 V, high |
| PLL with PC1 (10 k + 10 nF) | lock, VCO IN 3 V | 30.00 kHz, 3.00 V |
| 566, R1 10 k, C1 10 nF, V+ 12 V, VC 10.5 / 9 V | 2.5 / 5 kHz | 2501 / 5005 Hz |
| 566 triangle / square | 2.4 / 5.4 Vpp | 2.33 / 5.37 Vpp (10 k loads) |

Sources: TI CD4046B (SCHS043B, data from Harris), p. 3-124 to 3-128; Philips NE/SE566
product specification (15-Apr-1992), p. 400–401.

Books: AoE Figs. 13.85–13.104 (PLL, XOR and type II phase detectors, capture transient,
FM discriminator); Scherz Fig. 10.14 (566 VCO).
