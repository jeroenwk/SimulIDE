# MF5 switched-capacitor filter

A native part (`src/gui/circuitwidget/components/active/mf5.{h,cpp}`), Active / Other Active,
library name `MF5` ("SC Filter MF5"): one section of the MF10 universal filter, the way its
block diagram draws it. Pins by name: INV, S1, SA, CLK, L.SH, 50/100 (left); V+, N/AP/HP,
BP, LP, AGND, V− (right).

## Model

- **Summing op amp**: inverting input INV, non-inverting input at AGND, output N/AP/HP. A
  voltage-controlled source in the matrix (gain 10⁵, 10 Ω out) [INFERRED], so the external
  resistors close the loop in one solve, like the op-amp part.
- **Summer**: Σ = N − S1 − (SA above AGND ? LP : AGND). The signs follow from the datasheet's
  modes: with them, Mode 1 has its notch at f0 and Mode 4's LP gain is −(1 + R2/R1) and N
  is an allpass, as the equations say.
- **Integrators**: two non-inverting switched-capacitor integrators, Σ → BP → LP, stepped
  at each rising clock edge: BP += (2π/ratio)·Σ, LP += (2π/ratio)·BP, so f0 = fCLK / ratio.
  Like the real part the outputs are sampled data: a staircase at the clock rate.
  Outputs through 10 Ω, clipped 1 V inside the supplies [DOC].
- **50/100**: above mid-way to V+ → 50:1, near AGND → 100:1, below mid-way to V− → off
  (outputs held) [DOC].
- **CLK**: rising edges above L.SH + 1.5 V (0.2 V hysteresis) [INFERRED]; L.SH at ground
  takes TTL or ±5 V CMOS clocks, at +5 V a 0–10 V CMOS clock (single supply).

The external resistors pick the mode (MF10 datasheet modes 1–6): e.g. Mode 1 (R1 input to
INV, R2 N→INV, R3 BP→INV, SA to V+): f0 = fCLK/100 or /50, Q = R3/R2, HOBP = −R3/R1,
HOLP = HON = −R2/R1.

## Checks

`tests/regress/test_mf5.py` (13 checks, [SIMULATED]), ±5 V, 0.1 V sine in, 100 kHz clock:

| | Datasheet equations | Model |
|---|---|---|
| Mode 1 (Q 10, HOBP −10), at f0 = 1 kHz: BP, LP, N | 1.0, 1.0 V, notch | 0.998, 0.997 V, 6.6 mV |
| Mode 1 at 100 Hz: LP, BP | 0.101, 0.010 V | 0.1007, 0.0101 V |
| Mode 1 at 950 Hz: BP | 0.698 V | 0.686 V |
| Clock 50 kHz: BP at 500 Hz | 1.0 V | 0.991 V |
| 50/100 at V+: BP at 2 kHz | 1.0 V | 0.997 V |
| 50/100 at V−: filtering stops | — | 0 V |
| Mode 4: LP at 100 Hz (HOLP −2); N at 0.3 / 1 / 3 kHz (allpass) | 0.202; 0.1 V | 0.2015; 0.0999 / 0.1000 / 0.0996 V |

Source: TI MF10-N (SNOS547C, 2013), block diagram p. 2, pin descriptions, modes
p. 15–18. The MF5's own datasheet (SNOSBC9A) could not be fetched; its pin numbers are not
modelled (pins by name).

Left out: the op amp's finite bandwidth (the Q enhancement of Mode 3), clock feedthrough,
offsets, the maximum clock rate, supply current, the MF5's extra pins.

Book: Scherz Fig. 9.24 (MF5 universal filter, clock-tuned).
