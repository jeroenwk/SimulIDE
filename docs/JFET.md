# JFET

Junction FET, N or P channel, in the component list under *Active / Transistors / JFET*.
Drawn as a JFET (channel bar, gate arrow into the channel for N, out of it for P); drain on
top, source below, gate on the left.

## Model

SPICE level 1 (Shichman-Hodges), the model of SPICE's NJF/PJF, so SPICE model lines can be used
as they are (`src/simulator/elements/active/e-jfet.cpp`). In N-channel voltages:

| Region | Condition | Drain current |
|---|---|---|
| OFF | VGS < Vto | 0 |
| OHMIC | 0 < VDS < VGS − Vto | Beta · VDS · (2(VGS − Vto) − VDS) · (1 + Lambda·VDS) |
| ACTIVE | VDS > VGS − Vto | Beta · (VGS − Vto)² · (1 + Lambda·VDS) |

So IDSS = Beta·Vto² and gm = 2·√(Beta·ID). Drain and source swap when VDS < 0 (the channel is
symmetric). The gate is two junctions, gate–source and gate–drain:
IG = Is·(exp(V/(N·Vt)) − 1) each, so a forward-biased gate conducts like a diode. It is a full
Newton-Raphson element, like the BJT: a 1e-12 S minimum conductance keeps a cut-off channel and a
reverse gate in the matrix (picoamps of leakage).

Gate breakdown (`BV`, 0 = none): each gate junction also conducts Is·exp((−V − BV)/(N·Vt)) in
reverse, so past −BV the gate clamps like a Zener (2N5484–6: V(BR)GSS 25 V). The gate junction
capacitances `Cgs`, `Cgd` (SPICE CJ0 with `Pb`, `Mg`) come with the model.

Left out: the series resistances RD and RS (a few ohms in most models), impact ionization,
temperature.

## Properties

| Property | | |
|---|---|---|
| Model | a part from `resources/data/jfets.model`, or Custom | choosing one sets all the values below |
| P Channel | | |
| IDSS | drain current at VGS = 0 (in saturation, without Lambda) | changing it keeps the pinch-off voltage |
| Pinch-off Voltage (VGS) | Vto: negative for N channel, positive for P channel | changing it keeps IDSS |
| Lambda | output slope, 1/V | |
| Gate Saturation Current, Gate Emission Coefficient | Is and N of the gate junctions | |
| Gate Breakdown Voltage (`BV`) | reverse breakdown of the gate junctions, V (0 = none) | not part of a model |
| Cgs, Cgd, Pb, Mg | gate junction capacitances at 0 V, their potential and grading | set by the model (0 pF where the SPICE line has none) |
| Show State | OFF / ACTIVE / OHMIC badge and log (docs/PART_STATES.md) | |

Editing a value switches Model to Custom. `simulide cli read` gives `vgs`, `vds`, `i_drain`,
`i_gate` and `state`.

## Models

From LTspice's `standard.jft` (https://ltwiki.org/index.php?title=Standard.jft; manufacturer in
the model file). IDSS = Beta·Vto² at VDS = 0; at VDS = 10 V add Lambda·10.

| Model | Channel | IDSS (mA) | Vto (V) | Lambda (1/V) |
|---|---|---|---|---|
| 2N3819 | N | 11.7 | −3.0 | 0.00225 |
| 2N4416 | N | 9.3 | −3.06 | 0.0055 |
| 2N5457 | N | 2.1 | −1.37 | 0.0023 |
| 2N5484 | N | 3.2 | −1.2 | 0.0025 |
| 2N5485 | N | 6.5 | −2.5 | 0.00425 |
| 2N5486 | N | 11.1 | −3.89 | 0.0095 |
| BF245A | N | 3.5 | −1.74 | 0.018 |
| BF245B | N | 5.8 | −2.31 | 0.023 |
| BF245C | N | 13.6 | −5.0 | 0.027 |
| J111 | N | 93.5 | −5.36 | 0.02 |
| J112 | N | 32.4 | −2.33 | 0.023 |
| J113 | N | 15.3 | −1.29 | 0.03 |
| J201 | N | 0.58 | −0.6 | 0.0022 |
| MMBF5103 | N | 32.4 | −2.33 | 0.023 |
| J310 | N | 42.4 | −3.84 | 0.02 |
| MPF102 | N | 12.1 | −3.41 | 0.002 |
| 2N5460 | P | 3.4 | 1.75 | 0.02 |
| J175 | P | 37.5 | 3.63 | 0.02 |
| J176 | P | 15.3 | 2.3 | 0.02 |

The 2N5484/5/6 and 2N5460 use the file's J2N lines: its Siliconix lines give IDSS 6.8, 14, 64 and
11.6 mA, outside the datasheet ranges (1–5, 4–10, 8–20, 1–5 mA; create-circuit #622). A circuit
saved with the old values keeps them and loads as Custom.

These are one sample of each part: datasheets give wide ranges (2N3819: IDSS 2–20 mA,
VGS(off) down to −8 V). The 2N3819 against its Vishay datasheet (70238 Rev. D, typical column)
[SIMULATED with the model's equations]: IDSS 11.7 mA (typ 10), rDS(on) = 1/(2·Beta·|Vto|) = 128 Ω
(typ 150), gfs at VGS = 0 = 2·Beta·|Vto| = 7.8 mS (typ 5.5, max 6.5), gos = Lambda·IDSS = 26 µS
(typ 25), VGS(F) at 1 mA = 0.61 V (typ 0.7).

The MMBF5103 has no published SPICE model. onsemi's datasheet (MMBF5103/D Rev. 2) says "Sourced
from Process 51, see J111 for characteristics" and gives only limits: VGS(off) −1.2 to −2.7 V,
IDSS 10 to 40 mA. The J112 model (same process, Philips) falls inside both, so the MMBF5103 entry
uses its values [INFERRED]. It is the input JFET of *The Art of Electronics* Fig. 3.31.

To add a part, add a line to `jfets.model` from its SPICE `.model` (Beta, Vto, Lambda, Is, N; Cgs,
Cgd in pF, Pb, M) and
restart SimulIDE.

## Samples

`samples/jfet/`: a published 2N3819 amplifier that does not work (its divider forward-biases the
gate), the same with a source resistor, and the self-biased stage of Duncan's Fig. 52.1 (gain 22).
