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

In the GUI: the **noise button** in the toolbar (an orange jagged trace, next to the
instruments button) switches it on and off; a running simulation restarts, since the
noise starts with the simulation. **Settings → Simulation → Noise** has the details
(tick the group; *When*: start-up only, whole run or custom). From the CLI:

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

- **Toolbar:** an orange badge beside *Running* / *Paused*, with the noise icon in white
  and e.g. `on · ×1 · 0 s–1 ms` (state, scale, window; the tooltip has the bandwidth and
  the seed). It reads *armed* before the window, *on* during it and *ended* after it.
  The noise button among the view toggles shows it pressed. A narrow window puts the
  toolbar on two rows: file, edit, zoom and the view toggles on the first; speed, run,
  status, the badge, the time and Info on the second.
- **Messages:** at every start, `Noise on: 14 sources (9 thermal, 5 shot, 0 channel), ...`,
  and warnings for a `noiseOff` entry that matches nothing, or a `reactStep` that
  can't follow the noise.
- **Oscilloscope:** an orange `NOISE` tag in the corner of the screen while it shows
  samples taken with noise active (so screenshots carry it). It goes once the
  screen holds only samples from after the window.
- **CLI:** `sim status` (and so `sim run`) has `"noise": "off"`, or the settings,
  the state and the **seed in use**; `sim average` and `scope data` replies carry
  the same `noise` object when noise was active during their span; `probe` and
  `read` carry it once noise has been active in the run. `noise sources` lists
  every source.
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

**Logic start states:** with noise on, logic gates and flip-flops start in a random
state drawn from the noise seed (the same seed gives the same states), as at
power-up: a latch or a counter without a reset comes up differently from run to run.
A part in `noiseOff` keeps its own start state (a gate's Initial High State). After
the first solve the gates follow their inputs one by one with no delay, so only
states the logic allows remain (a latch keeps its random state, a ring of gates
starts with one gate pending).

**Gate delay jitter:** with noise on, every output change of a gate or flip-flop
comes up to ±2 % of its delay early or late, from its own seeded sequence (for the
whole run, not only the noise window; `noiseOff` parts keep exact delays). Two gates
that switch at the same instant (two equal RC halves of a latch crossing their
thresholds together) otherwise send a pulse of exactly one gate delay round the
latch for ever, at 1/(2·tpd); with jitter the pulse falls short of a delay and the
gate drops it, as a real latch resolves. A ring of inverters runs within the jitter
of its frequency.

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

The **inductor** and the **resistor** have the same kind of capacitance as a property:
`Cp`, a constant capacitance across their two pins (a coil's winding capacitance, e.g.
the led_driver's T1: 17.9 pF from its self-resonance; a resistor's 0.05–0.5 pF, which
only matters for high values at high frequency). A relay's coil has it too (group
*Coil*): without it, a coil switched off with no diode gives a spike one reactive step
wide (its current has nowhere else to go, and a BJT without `BVceo` has no breakdown);
with it, the coil rings. A BJT's `BVceo` (C-E breakdown voltage, 0 = none) clamps
the collector there instead, as a real transistor's avalanche does, until the coil's
current has drained (state BREAKDOWN). 0 (the default) means none, with or
without noise, and costs nothing: the inner capacitor then has no pins and no events.
Its current is not in the part's own current readout (the inductor's gauge, a
resistor's V/R), as above. Capacitors already have their series resistance (ESR) as
`Resistance`.

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
  circuit ends at the values of a run without noise. Measured (Apple M-series,
  headless, speed unthrottled, 500 ms runs), wall seconds per simulated second:

  | Circuit | off | 1 ms window, 10 MHz | whole run, 1 MHz | whole run, 10 MHz |
  |---|---|---|---|---|
  | Duncan 73.7 bistable (21 parts, 1 kHz trigger) | 0.56 | 0.85 | 1.46 | 10.8 |
  | `tests/circuits/noise_scope_demo.sim1` (9 parts) | 0.35 | 0.47 | 0.57 | 3.0 |

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

## Demo

`tests/circuits/noise_scope_demo.sim1`: a 50 µV, 1 kHz sine into 1 MΩ ∥ 10 pF,
amplified ×1000 by an (noise-free) op amp: the yellow trace is the sine with the
node's √(kT/C) = 20 µV rms noise (20 mV at the output); the blue trace is the same
path with its resistor in `noiseOff`, a clean reference. Toggle the toolbar noise
button to compare. Measured: 20.6 µV against 20.4 µV for kT/C.

## Checks

`tests/regress/test_noise_*.py`:

| Test | Checks |
|---|---|
| `test_noise_ktc` | 1 kΩ ∥ 1 nF: v<sub>rms</sub> = √(kT/C) = 2.04 µV (measured 2.02 µV), ×100 scale |
| `test_noise_diode_ktc` | shot noise of a diode with C across: √(n·kT/2C) at 0.13 mA and 1.1 mA |
| `test_noise_sources` | S of every kind against 4kT/R, 2q·I from the solved currents; noiseOff by label, type, typo |
| `test_noise_seed_window` | same seed = same run, reported random seed repeats it, the window ends exactly |
| `test_noise_settings` | off by default, files unchanged, save/reopen, status, Messages |
| `test_logic_startup` | gates settled at the start (a ring of N inverters at 1/(2N·tpd)); with noise, a NAND latch and a D flip-flop start in both states over the seeds, the same seed repeats, noiseOff keeps the part's start |
| `test_cc-268_latch_lockstep_jitter` | a NAND latch with cross-coupled RC timing (Scherz 12.79D) runs at its RC period (430 Hz) over two seeds, not at 1/(2·tpd); the same seed repeats |
| `test_noise_bistable` | a symmetric bistable ends in a seed-dependent state, with 10 nF base capacitors and with none (junction capacitance); without noise the resistive one stays balanced |
| `test_noise_reply_tag` | `probe`, `read`, `scope data`: no tag without noise, a tag inside the window, `scope data` of noise-free samples untagged after it, a next run without noise untagged |
| `test_inductor_cp` | `Cp` 17.9 pF across 374 µH gives the same voltages as a separate capacitor (within 1 nV), rings at 1/(2π√LC); 0 = no ring; save and reopen |
| `test_relay_cp` | a relay coil's `Cp` 17.9 pF rings the same as an Inductor's (within 1 µV); 0 = no ring; save and reopen |
| `test_resistor_cp` | `Cp` 10 pF on a 1 MΩ / 1 MΩ, 100 pF divider: the step to 5·10/110 V, then τ = 55 µs within 1 %; same as a separate capacitor; 0 = none; save and reopen |
| `test_junction_cap` | the led_driver D3A C(V) (14.67 pF, 0.363 V, 0.319): charging through 1 MΩ within 0.4 % of the integrated curve; 0 = none; save and reopen |

The bistable of `test_noise_bistable` is built by the test itself (CLI), so it can be
rebuilt in the GUI the same way. `python3 tests/regress/sweeps/sweep.py header` puts
junk in every noise attribute of a saved header and opens and runs it (crash, hang,
NaN), and checks that each setting survives a save and reopen.
