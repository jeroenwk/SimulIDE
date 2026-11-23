# Quartz crystal

A native part (`src/gui/circuitwidget/components/passive/reactive/crystal.{h,cpp}`), Passive /
Reactive, pins `lPin`, `rPin`: the Butterworth–Van Dyke equivalent circuit, a motional branch
L1–C1–R1 with the shunt capacitance C0 across it.

| Property | | |
|---|---|---|
| `Model` | 32.768 kHz, 4 MHz (default), 10 MHz, Custom | |
| `Frequency` | series resonance fs | L1 = 1 / ((2π fs)² C1) |
| `C1` | motional capacitance (pF: 18 fF is 0.018 pF; SimulIDE reads no `f` prefix) | |
| `R1` | motional resistance (ESR) | Q = 2π fs L1 / R1 |
| `C0` | shunt capacitance | fp = fs √(1 + C1/C0) |
| `Steps` | integration steps per cycle of fs (default 40, at least 8) | |
| `Start_Current` | motional current at t = 0 (0 = none) | skips the start-up |

CLI `read`: `i_motional`, `l1`, `q`, `fs`, `fp`.

## Why its own integration

SimulIDE's capacitors and inductors use backward Euler, whose numerical damping is about
reactStep / C of series resistance near a resonance: for C1 = 18 fF at a 5 ns step that is
280 kΩ, against an R1 of 50 Ω. The crystal integrates both branches with the trapezoidal
rule at its own step (fs × Steps), which keeps an LC loop's energy: Q stays 10⁴–10⁵. The
trapezoidal rule shifts a resonance down (ωd = (2/h)·atan(ωa h/2)): L1 is pre-warped so the
discrete series resonance falls exactly on fs. The crystal reads the voltage solved at its
own step times (`stepSettled()`) and stamps the next step's companion currents at the next
event: other parts' events in between would otherwise shift the sample time, and that phase
error damps a high-Q loop.

## Using it

- **The rest of the circuit needs a fine `reactStep`** (Settings or `settings set
  reactStep=5ns`): load capacitors of 33 pF at the default 1 µs step see four cycles of
  4 MHz per step. About fs × 50 steps is enough.
- **Start-up takes about Q / π cycles** of exponential growth from noise: 14 000 cycles
  (3.5 ms) at 4 MHz, 60 000 cycles (2 s) at 32.768 kHz. A symmetric circuit sits at its DC
  point for ever without noise. `Start_Current` (e.g. 0.05 mA) starts the motional branch
  ringing at once; lowering Q (a larger R1) shortens the start-up.
- Simulation cost: an event every 1 / (fs × Steps): 160 million per simulated second at
  4 MHz. Simulate milliseconds, not seconds.

## Values of the models

[INFERRED] typical values, from vendor tables (HC-49 crystals: C1 18–20 fF typical, C0
≤ 7 pF; ESR within the series' maximum; watch crystals: C1 a few fF, ESR tens of kΩ):

| Model | fs | C1 | R1 | C0 | L1 | Q |
|---|---|---|---|---|---|---|
| 32.768 kHz | 32.768 kHz | 2 fF | 35 kΩ | 1.5 pF | 11.8 kH | 69 000 |
| 4 MHz | 4 MHz | 18 fF | 50 Ω | 5 pF | 88 mH | 44 000 |
| 10 MHz | 10 MHz | 18 fF | 25 Ω | 5 pF | 14 mH | 35 000 |

Use the crystal's own datasheet values for a given part (Custom).

## Checks

`tests/regress/test_crystal.py` (8 checks against hand calculations, [SIMULATED]):

| | Hand | Model |
|---|---|---|
| L1, Q of the 4 MHz model | 87.96 mH, 44 200 | 87.95 mH, 44 210 |
| Driven at fs through 50 Ω, 1 V | 0.50 V across (R1) | 0.508 V |
| Ring-down across 10 MΩ: frequency | fp = 4 007 194 Hz | 4 007 206 Hz |
| Ring-down: amplitude ratio over 1 ms | 0.7260 (τ = 2 L1 / (R1 + 6.3 Ω)) | 0.7258 |
| Pierce oscillator (inverting 0.1 S transconductance, 2 × 33 pF) | grows | 51 → 70 µA in 300 µs |

What the model leaves out: overtones and spurious modes, drive-level dependence,
temperature (the frequency is fixed), ageing, and the pulling of a real crystal by C0's
package capacitance beyond C0 itself. A BJT Pierce stage needs enough gain at the base
node: its r_π across the load capacitor can damp the loop (a 1 kΩ / 100 kΩ common-emitter
stage with 33 pF did not start).

Books: AoE Fig. 7.38 (Pierce and other crystal oscillators); Scherz Figs. 10.20–10.22
(equivalent circuit, Colpitts and Pierce crystal oscillators).
