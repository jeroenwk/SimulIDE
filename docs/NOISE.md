# Transient noise

SimulIDE can add the physical noise of the parts to a simulation: the thermal noise
of resistors and the shot noise of diodes and transistors, at the level the real
parts make. It is **off by default** and belongs to the circuit (it is saved in the
`.sim1` file). The design and its reasons are in [NOISE_SPEC.md](NOISE_SPEC.md).

Use it to:

- let a symmetric circuit (bistable, flip-flop, oscillator) leave its balance point
  the way a real one does, with a random outcome (*start-up noise*);
- see the noise floor of an amplifier, a comparator or a sampled signal.

## Switching it on

In the GUI: **Settings → Simulation → Noise** (tick the group; *When*: start-up only,
whole run or custom). From the CLI:

```sh
simulide cli settings set noise=on noiseStop=1ms   # start-up only: noise for the first 1 ms
simulide cli settings set noise=on noiseStop=0     # the whole run
simulide cli sim start
```

| Setting | Default | |
|---|---|---|
| `noise` | off | on / off |
| `noiseBW` | 10 MHz | Noise bandwidth f<sub>max</sub>. Every source draws a new value every 1/(2·f<sub>max</sub>) (50 ns at 10 MHz) |
| `noiseScale` | 1 | Multiplies every noise current. 1 is the physical level |
| `noiseSeed` | 0 | 0: a new random seed at every start. Any other value repeats the same noise exactly |
| `noiseStart` | 0 | Noise from this simulated time... |
| `noiseStop` | 0 | ...until this time (0: until the end) |
| `noiseOff` | | Parts without noise: labels (`R3`, inside a subcircuit `U1-R2`) or types (`type:Resistor`), comma-separated; `none` clears it |

Settings take effect at the next start. The circuit's temperature is 27 °C.

## Seeing that it is on

- **Toolbar:** an orange label, e.g. `NOISE on · ×1 · 10 MHz · seed 48213 (random) · 0 s–1 ms`.
  It reads *armed* before the window, *on* during it and *ended* after it.
- **Messages:** at every start, `Noise on: 14 sources (9 thermal, 5 shot, 0 channel), ...`,
  and warnings for a `noiseOff` entry that matches nothing, or a `reactStep` that
  can't follow the noise.
- **CLI:** `sim status` (and so `sim run`) has `"noise": "off"`, or the settings,
  the state and the **seed in use**; `sim average` replies carry the same `noise`
  object when noise was active during the span. `noise sources` lists every source.
- **File:** the `<circuit>` line gets `noise="1" noiseBW=... ` (only when noise was
  ever set; other files are unchanged).

A random run can be repeated: take `seed` from `sim status` and
`settings set noiseSeed=<seed>`.

## What makes noise

| Part | Noise | Spectral density (A²/Hz) |
|---|---|---|
| Resistor, resistor array, potentiometer (both halves), variable resistor, LDR, thermistors, RTD, strain gauge | thermal | 4kT/R |
| Diode, Zener, LED | shot | 2q·\|I\| |
| BJT (NPN, PNP) | shot, collector–emitter and base–emitter | 2q·\|I<sub>C</sub>\|, 2q·\|I<sub>B</sub>\| |
| MOSFET, JFET | channel thermal | 4kT·(2/3)·g<sub>m</sub> |

The current and g<sub>m</sub> are those of the moment, so shot noise follows the bias.
Parts made of these (subcircuits such as ICs) get noise from their inside parts.

**Make no noise:** sources and batteries, switches, relays, meters and probes,
wires, lamps, motors, the audio output, voltage regulators, op-amps and comparators
(behavioural models), logic and microcontrollers. There is no 1/f (flicker) noise.

## Junction capacitance

Diodes, Zeners, LEDs, BJTs, MOSFETs and JFETs have capacitance properties (group
*Capacitance*), the SPICE depletion model:

  C(V) = Cj0 / (1 − V/Vj)<sup>M</sup>  (V forward; linear above V = Vj/2, as in SPICE)

| Part | Properties | Typical value used while noise is on and the property is 0 |
|---|---|---|
| Diode, Zener | `Cj0`, `Vj` (0.7 V), `Mj` (0.33) | 4 pF |
| LED | `Cj0`, `Vj`, `Mj` | 20 pF |
| BJT | `Cje`, `Vje`, `Mje` (B–E); `Cjc`, `Vjc`, `Mjc` (B–C) | 4.5 pF, 3.6 pF |
| MOSFET | `Cgs`, `Cgd` (constant) | 20 pF, 5 pF |
| JFET | `Cgs`, `Cgd`, `Pb` (0.6 V), `Mg` (0.5) | 3 pF, 3 pF |

- A value above 0 is used in **every** simulation, with or without noise: set a
  measured or datasheet value (e.g. the led_driver's D3A: `Cj0` 14.67 pF, `Vj`
  0.363 V, `Mj` 0.319, from its ring test).
- 0 (the default) means no capacitance, as before, except while noise is on: then
  the typical value above is used, so a loop has the dynamics of real parts
  (Messages says how many junctions got one).
- The capacitance integrates at `reactStep` like a capacitor: a few pF only shows
  when `reactStep` is small next to the time constants around it.
- Its current flows through its own pins: a meter in series sees it, the part's own
  current readout (and part state) does not.

## Things to know

- **Bandwidth and step.** Each source is a Gaussian value held for
  1/(2·f<sub>max</sub>): white up to about f<sub>max</sub> (−3.9 dB there, a sinc roll-off). Choose
  f<sub>max</sub> about 5× the highest frequency of interest. During the noise window every
  capacitor, inductor and junction capacitance integrates with that noise step
  (when it is finer than its own), and goes back to its own step after the window.
  Transformers keep `reactStep` (Messages warns when it is coarser).
- **Speed.** Without noise SimulIDE only solves when something changes. With
  noise every node changes 2·f<sub>max</sub> times per simulated second (2·10⁷ at
  10 MHz). Use the window (`noiseStop`) for start-up noise: after it the run is
  as fast and as exact as without noise. Every current is exactly 0 again, so the
  circuit ends at the values of a run without noise.
- **Positive feedback needs dynamics and a step that can follow it.** A loop with
  no capacitance has no dynamics: each step's solve lands on the balance point
  again. And backward Euler is stable even at an *unstable* point when the growth
  rate is above about 2/step. Both are why a symmetric bistable can sit at its
  balance point without noise. With noise on, the junction capacitances (above)
  give the loop its dynamics and the fine step in the window lets it grow: the
  test bistable without any capacitor flips at random with the default 10 MHz,
  0–1 ms, and the state holds after the window at the normal step. A very fast
  loop needs a higher `noiseBW` (finer step).
- **Physical levels are small.** Thermal noise of 1 kΩ over 10 MHz is 13 nA rms.
  A positive-feedback loop amplifies it, but needs about ln(1 V / 1 nV) ≈ 21 of
  its time constants to reach full swing. For a slow loop use a longer window or
  `noiseScale` > 1 (and say so in results).
- **Current readouts.** The noise current of a part flows through its own pins of
  the noise engine (the Norton picture): a meter in series sees the total current;
  a resistor's own current readout (V/R) does not include its noise source.
- Results with noise are [SIMULATED] with noise: give the settings (seed, scale,
  window) with them.

## Checks

`tests/regress/test_noise_*.py`:

| Test | Checks |
|---|---|
| `test_noise_ktc` | 1 kΩ ∥ 1 nF: v<sub>rms</sub> = √(kT/C) = 2.04 µV (measured 2.02 µV), ×100 scale |
| `test_noise_diode_ktc` | shot noise of a diode with C across: √(n·kT/2C) at 0.13 mA and 1.1 mA |
| `test_noise_sources` | S of every kind against 4kT/R, 2q·I from the solved currents; noiseOff by label, type, typo |
| `test_noise_seed_window` | same seed = same run, reported random seed repeats it, the window ends exactly |
| `test_noise_settings` | off by default, files unchanged, save/reopen, status, Messages |
| `test_noise_bistable` | a symmetric bistable ends in a seed-dependent state, with 10 nF base capacitors and with none (junction capacitance); without noise the resistive one stays balanced |
| `test_junction_cap` | the led_driver D3A C(V) (14.67 pF, 0.363 V, 0.319): charging through 1 MΩ within 0.4 % of the integrated curve; 0 = none; save and reopen |

The bistable of `test_noise_bistable` is built by the test itself (CLI), so it can be
rebuilt in the GUI the same way.
