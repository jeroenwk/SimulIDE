# Current Source IC

Three-terminal programmable current source (LM334 type), in the component list under
*Active / Other Active / Current Source IC* (`itemtype="CurrReg"`). A box with the current-source
symbol: **V+** on top, **V−** below, **R** on the right. The set resistor R_SET goes from R to V−;
the current flows in at V+ and out at V− (and through R_SET). It is a true floating
2-terminal source: no supply pin, so it can sit on the high side, the low side or between two
moving nodes.

The **Model** list comes from `resources/data/currregs.model`; new parts are added there
(see [Adding a model](#adding-a-model)).

## Model

All voltages relative to V−: v = V+ − V− (headroom), u = V_R − V− (across R_SET).

| Part of the model | Equation | What it gives |
|---|---|---|
| Sense loop | F = G(v)·(Vsense − u), 0 when u > Vsense; F flows V+ → R | V_R held at Vsense (64 mV for the LM334) |
| Turn-on | G(v) = 1/(1/Ga + 1/Gb + 1/100 S), log₁₀ Ga = ga + sa·(v − 0.6), log₁₀ Gb = gb + sb·(v − 0.6) | with R_SET: I = Vsense/R · G·R/(1 + G·R), the datasheet's turn-on curves (lower currents turn on at lower voltage) |
| Bias current | B = F/(n − 1), V+ → V−; n from the model's `ratio` curve against the current | I_SET = n/(n − 1)·Vsense/R_SET = 67.7 mV/R_SET for n = 18 |
| Line regulation | X = (F + B)·L(v), V+ → V−; L = klo·(v − 2.5 V) up to vknee, then khi per V | the finite output impedance (0.02 %/V, 0.01 %/V above 5 V) |
| Temperature | Vsense·T/298.15 K with `ptat="1"` | I_SET proportional to absolute temperature: the part as a sensor |
| Reverse | grev·v for v < 0 | a few dozen µA at −20 V |

A Newton-Raphson element like the BJT and JFET (`src/simulator/elements/active/e-currreg.cpp`):
the full 3×3 Jacobian is stamped as admittances between the pins, and a 1e-12 S minimum
conductance between every pair keeps an off part in the matrix. Newton steps of the headroom go
through the steep turn-on band (0.3 to 1.3 V) 50 mV at a time. Without that, a source fed
through a load resistor jumped between "no headroom, off" and "full headroom, saturated" and
never converged. Line regulation is an extra current, not a change of Vsense: with a Vsense that
moved with the headroom, a large headroom drop left V_R just above it and the part switched off.

**Left out:** the 15 pF shunt capacitance and the slew-rate limit (datasheet p.3, p.6: the real
part rings for a few µs after a fast step), self-heating (≈ 0.4 °C per V at 1 mA, p.6),
noise, and the spread between parts (±3 % LM134/LM234, ±6 % LM334; the model is the typical
part). The current is fixed by the R_SET you draw: contact resistance in series with R_SET counts,
as on the real part (0.7 Ω is −1 % at 1 mA, p.7).

## Properties

| Property | | |
|---|---|---|
| Model | a part from `currregs.model`, or Custom | choosing one sets the values below and the hidden ones (turn-on, line regulation, ratings) |
| Junction Temperature (`Temp`) | °C, default 25 | Vsense scales with absolute temperature (PTAT models) |
| Show State | the state badge and log | |
| Sense Voltage (`Vsense`) | V across R_SET at 25 °C | |
| I_SET / I_BIAS (`Ratio`) | typical ratio n | editing it replaces the model's ratio curve with this constant |

Editing a value switches Model to Custom; the hidden values stay those of the last model.

## States and readout

| State | Level | When |
|---|---|---|
| REG | on (green) | V_R within 1 % of Vsense (leaves at 2 %) |
| LOW V | wait (amber) | below the compliance voltage: V_R short of Vsense |
| OFF | off | no current: no headroom, reversed, or R open |
| OVER V / REVERSE V / OVER I / OVER P / R PIN V | fault (red) | an absolute maximum rating exceeded: V+ − V− > vmax, < −vrev, I_SET > imax, power > pmax, \|V_R\| > vrmax |

`simulide cli read U1` gives `voltage` (V+ − V−), `v_r`, `i_set` (into V+), `i_r` (out of R),
`i_bias` (out of V−), `power` and `state`.

## Models

| Model | Vsense | n | Turn-on | Line reg. | Ratings | Source |
|---|---|---|---|---|---|---|
| LM334, LM234, LM134 | 64 mV at 25 °C, PTAT | 18 typ; curve 16.1 (10 µA), 18.2 (180 µA), 17.1 (1 mA), 12.6 (5.6 mA) | 94–97 % of I_SET at 0.8 V/100 µA, 0.9 V/1 mA, 1.0 V/5 mA; ≈ 1 µA at 0.5 V | 0.02 %/V (1.5–5 V), 0.01 %/V (5–40 V) | 40 V, −20 V, 10 mA, 400 mW, R pin 5 V | TI SNVS746E (2013) |

The three LM parts differ in temperature range and initial accuracy only, not in the typical
values the model uses.
`ga sa gb sb` are [INFERRED]: a least-squares fit to the datasheet's turn-on curves (p.5 Fig 11,
R_SET 14 Ω to 6.8 kΩ) with p.3's minimum operating voltages as anchors. It is within 20 % on
the rising edge except one point (6.8 kΩ at 0.6 V, +64 %), and the knee is 3 to 6 % softer
than the curve (68 Ω at 0.9 V: 0.97 of I_SET against the figure's 0.99).
`grev` is [INFERRED] from p.1 ("a few dozen microamperes" at 20 V reversed): 30 µA.

Checked by `tests/regress/test_currreg_lm334.py` [SIMULATED]:

| Check | Datasheet | Model |
|---|---|---|
| I_SET, V+ = 2.5 V, R_SET 6.8 k / 680 / 68 / 14 Ω | 67.7 mV/R_SET (±3–8 %) | +0.4 / +0.1 / +0.3 / +2.0 % |
| V_R | 64 mV | 64.0 mV (63.95 at 14 Ω) |
| I_SET/I_BIAS at 100 µA / 1 mA | 14–26, typ 18 | 17.95 / 17.12 |
| Line regulation 1.5–5 V / 5–40 V | 0.02 / 0.01 %/V typ | 0.0200 / 0.0100 %/V |
| 680 Ω at 0.8 V | working (min. operating voltage) | 96.7 µA (97 %) |
| 680 Ω at 0.5 V | ≈ 1 µA (Fig 11) | 0.66 µA |
| 75 °C / 25 °C | 348.15/298.15 = 1.168 | 1.168 |
| −20 V | a few dozen µA | 30 µA |
| 1 mA source, 15 V, load 10 k / 14 k / 15 k / 20 k | Fig 11 at the headroom left | 1.00 / 0.999 / 0.94 / 0.71 mA, path-independent |
| 1 mA into 1 µF for 5 ms | 5 V ramp | 5.003 V |

## Adding a model

Add an `<item>` to an `<itemset type="programmable">` in `resources/data/currregs.model`,
with the attributes listed in its header comment (missing ones take the LM334 values), and
note the datasheet pages for each. Rebuild, or copy the file into the built app's `data/`
folder; the list is read at startup. Kinds of part that need more than this:

- **Fixed current sources** (REF200's 100 µA cells, two-terminal current-regulator diodes such
  as the J500 / CR series): a new itemset type, whose items give the current instead of
  Vsense. The engine then uses an internal set resistor Vsense/I and leaves the R pin
  unconnected. Not written yet.
- A different turn-on law or temperature law: new attributes with defaults that keep the
  current models unchanged.
