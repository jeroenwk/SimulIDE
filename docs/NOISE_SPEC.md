# Transient noise: specification

Status: **draft, not implemented** (2026-11-14). Fork feature of `jeroenwk/SimulIDE`.

The simulator gets a setting that adds the physical noise of the parts to a time
simulation, the way Cadence Spectre ("transient noise") and SIMetrix ("real time
noise") do it. The setting belongs to the circuit, is **off by default**, and when
it is on it is shown in the GUI, the CLI replies and the saved file.

Decided by the user (2026-11-14):

- **Physical noise** from the parts' own values and operating points, generic for
  every resistor and junction. No rules for single parts.
- A **noise source part** (white, 1/f, hum, ripple, random telegraph noise, like
  ngspice `TRNOISE`) comes **later**, as phase 3.

## 1. Why

- **Start-up.** A symmetric oscillator or bistable (an astable or bistable
  multivibrator, an RC or LC oscillator, a Schmitt-trigger relaxation oscillator)
  can sit at its unstable balance point in the simulator forever, because every
  node voltage is computed exactly. A real circuit leaves that point within
  microseconds, because noise is amplified by the positive feedback. Today this is
  worked around by hand, with initial conditions, kick pulses or asymmetric values
  (create-circuit issues #86–88, `lim-osc`).
- **Realism, when it is asked for.** Thermal noise and shot noise set the noise
  floor of amplifiers, of sampled signals and of comparator decisions.

## 2. What professional simulators do (summary)

| | Spectre | SIMetrix | ngspice | LTspice |
|---|---|---|---|---|
| Source of noise | the device noise models | the device noise models | explicit `TRNOISE` / `TRRANDOM` sources | none |
| Switch | `noisefmax` > 0 on `tran` | `RTNstep` > 0 on `.TRAN` | the source is in the netlist | — |
| Bandwidth or update | `noisefmax`, `noisetmin` | `RTNstep` | sample time `NT` | — |
| Scale | `noisescale` | — | amplitude | — |
| Seed | `noiseseed` | — | — | — |
| Time window | — | `RTNstart`, `RTNstop` | — | — |
| Which parts | `noiseon`, `noiseoff` | all | where placed | — |

This feature takes the switch, bandwidth, scale, seed and part list from Spectre
and the time window from SIMetrix. Sources are listed in §13.

## 3. Behaviour

### 3.1 Settings (circuit level, in the simulation group)

| Name | Type, default | Meaning |
|---|---|---|
| `noise` | bool, `false` | Master switch. Off: the simulator behaves exactly as it does now, bit for bit |
| `noiseBW` | frequency, `10 MHz` | Noise bandwidth f<sub>max</sub>. The sources are redrawn every Δt = 1/(2·f<sub>max</sub>) (50 ns at 10 MHz) |
| `noiseScale` | real ≥ 0, `1` | Multiplies every noise current (×1 = physical). A factor *k* multiplies the noise power by *k*² |
| `noiseSeed` | integer ≥ 0, `0` | 0 = a new random seed at every start, which is reported. Any other value gives exactly the same noise every run |
| `noiseStart` | time, `0` | Noise is on from this simulated time... |
| `noiseStop` | time, `0` | ...until this time. 0 = until the end of the run |
| `noiseOff` | list, empty | Parts that make no noise (Spectre `noiseoff`). An entry is a part (its label or id, e.g. `R3`, `Q2`; inside a subcircuit `X1-R2`) or a whole part type as `type:NAME` with the type name of the library (`type:Resistor`, `type:Diode`, `type:BJT`), e.g. `R3,type:Potentiometer` |

- The temperature is fixed at 300.15 K (27 °C), the same as the thermal voltage
  V<sub>t</sub> = 25.865 mV that the diode and BJT models already use.
- **Window presets** (dialog and documentation): *start-up only* = `0` to `1 ms`;
  *whole run* = `0` to `0`.
- After `noiseStop` all noise currents go back to exactly 0 and the noise element
  schedules no more events, so the rest of the run is as fast and as exact as
  without noise.

### 3.2 Which parts make noise (v1)

Each formula gives a one-sided power spectral density *S* (A²/Hz) of a current
source in parallel with the part's two nodes. It is evaluated at the operating
point of the moment, at every update, so shot noise follows the bias current.

| Model (`src/simulator/elements/`) | Parts using it | Noise | *S* |
|---|---|---|---|
| `eResistor` with the new noisy flag (§5.3) | Resistor, ResistorDip, VarResistor, Potentiometer (its two halves), LDR, thermistors, strain gauge, varistor | thermal | 4kT/R |
| `eDiode` (and `eLed` through LedBase) | diode, Zener, LEDs | shot | 2q·\|I\| |
| `eBJT` | NPN, PNP | shot: collector C–E and base B–E | 2q·\|I<sub>c</sub>\|, 2q·\|I<sub>b</sub>\| |
| `eMosfet` | N/P MOSFET | channel thermal | 4kT·(2/3)·g<sub>m</sub> |
| `eJfet` | N/P JFET | channel thermal | 4kT·(2/3)·g<sub>m</sub> |

- **Parts not in v1:** switches, relays, wires, meters, sources and their internal
  resistance, batteries, lamps, motors, the audio output, voltage regulators,
  behavioural op-amps and comparators, logic gates and MCUs. They use `eResistor`
  for an internal resistance that is not a physical resistor, or they have no
  device-level model. They make no noise, which is documented. Op-amp input noise
  (e<sub>n</sub>, i<sub>n</sub>) can come later with the noise source part.
- **Subcircuits** made of these parts (e.g. the 555's internal divider) get noise
  automatically, because their parts are the same elements.
- **Flicker (1/f) noise** is not in v1. The source interface (§5.2) takes a PSD
  shape so that it can be added later.
- g<sub>m</sub> of the FETs is taken from the model at the operating point: ∂I<sub>D</sub>/∂V<sub>GS</sub>
  from the model's own formula, or a finite difference of it, and 0 in cut-off.

### 3.3 Sampling (how a PSD becomes a current)

Each source holds a Gaussian value for Δt = 1/(2·f<sub>max</sub>) and then draws a new one:

  i = noiseScale · √(S · f<sub>max</sub>) · N(0,1)

A sample-and-hold white sequence with variance σ² and hold time Δt has a
one-sided PSD of 2σ²Δt = σ²/f<sub>max</sub> at low frequency, so σ² = S·f<sub>max</sub> gives the
right density. The hold rolls the level off as sinc: −3.9 dB at f<sub>max</sub>. The
documentation says so, and that f<sub>max</sub> should be about 5× the highest frequency
of interest.

**Check against the classic result:** a resistor R with a capacitor C across it
gives v<sub>rms</sub> = √(kT/C), whatever R is, as long as f<sub>max</sub> ≫ 1/(2πRC)
(64 µV for 1 pF, 2.0 µV for 1 nF). This is the main test (§9).

### 3.4 Interaction with the solver

- Noise needs the reactive step to follow it: when `reactStep` > Δt, capacitors
  integrate the noise with a coarser step than it changes. At start, when
  `reactStep` > Δt, the Messages pane and the CLI give the warning: "noise
  bandwidth 10 MHz needs reactStep ≤ 50 ns (now 1 µs): the noise level across
  capacitors will be wrong". The setting is not changed silently.
- Cost: SimulIDE only solves when something changes. With noise, every node
  changes at every update, so the matrix is solved 2·f<sub>max</sub> times per simulated
  second (2·10⁷ at 10 MHz), plus the non-linear iterations. The documentation
  gives this number, and that the window (start-up only) avoids the cost.
- The diode model only re-linearises when its voltage moves by more than 10 mV,
  and the BJT model when it moves by more than 1 mV. Noise smaller than that
  propagates through the linearised (small-signal) model. That is the correct
  behaviour for small noise, and it needs no change.
- Current readouts: the noise current flows through the noise element's own pins
  (§5.1). An ammeter or a probe in series measures the real total current. The
  part's own `current()` (V/R for a resistor) does not include its noise source.
  This is the usual Norton picture and is documented.

### 3.5 Visibility ("it must be clear that noise is on")

1. **Status bar (InfoWidget):** while `noise` is on, a permanent orange label
   `NOISE ×1 · 10 MHz · seed 48213 · 0–1 ms`. It is there in every state: before
   the run it says "armed", during the window "on", after `noiseStop` "ended".
   When noise is off there is no label.
2. **Messages pane, at every start:** one information line:
   `Noise on: 14 sources (9 thermal, 5 shot), BW 10 MHz, ×1, seed 48213, window 0–1 ms`,
   plus the reactStep warning of §3.4 when it applies.
3. **CLI `sim status`:** always has a `noise` field: `"noise": "off"`, or
   `{"on":true, "active":true|false, "bw":"10MHz", "scale":1, "seed":48213,
   "seedGiven":false, "start":"0s", "stop":"1ms", "sources":14, "off":["R3"]}`.
   `seed` is the seed actually used, so a random run can be repeated with
   `settings set noiseSeed=48213`.
4. **CLI measurement replies** (`sim run`, `sim average`, `scope data`, `probe`,
   `meter`) get `"noise": {...}` (the same object) whenever noise was active during
   the measured span. Tools then cannot report a noisy number without seeing it.
5. **Saved file:** the `<circuit>` header gets the attributes (§6). The settings
   dialog shows them.
6. **Oscilloscope and plotter screen (optional, step 6):** a small "NOISE" tag in
   the corner of the display while it shows samples taken with noise active, so
   screenshots carry it too.
7. **New CLI command `noise sources`:** one row per source: part id, kind
   (thermal / shot / channel), the two nodes, S and the rms current of the last
   update. Used by the tests and for debugging.

## 4. Architecture overview

```
Simulator ── NoiseSettings (the §3.1 values) + the seed in use
   │
   └─ eNoise : eElement      (one per run, only when noise is on)
        ├─ initialize(): collect the sources from every eElement (noiseSources())
        ├─ stamp():       a pair of ePins per source, on the part's two eNodes
        ├─ runEvent():    every Δt inside the window: draw, stampCurrent(±i), addEvent(Δt)
        └─ at noiseStop:  stampCurrent(0) on every pin, no more events
```

It is event-driven, like the clocked elements: the event loop in
`Simulator::runCircuit()` and `solveCircuit()` stay unchanged.

## 5. Implementation details

### 5.1 Why the noise has its own pins

`eNode::stampCurrent( epin, value )` *replaces* the value of that pin's connection;
it does not add to it. A noise current stamped on a part's own `ePin` would
therefore overwrite the part's companion current (the diode and BJT stamp theirs
in `voltChanged()`). So `eNoise` creates two `ePin`s of its own per source:

- `setEnode( partPin->getEnode() )` and `createCurrent()`, the same way `eBJT`
  attaches its internal junction pins (`m_BE->setEnode( baseNod )` in
  `e-bjt.cpp`).
- A source between nodes a and b stamps `pinA->stampCurrent( +i )` and
  `pinB->stampCurrent( -i )`.
- A pin whose node is `NULL` (unconnected) is skipped: `stampCurrent` already
  does that.
- `eNode::clear()` at the next `createNodes()` detaches the pins. `eNoise` deletes
  its pins in its destructor and at `stopSim()`.

### 5.2 Source interface (`src/simulator/noise.h`, new)

```cpp
struct NoiseSrc {
    ePin* a; ePin* b;            // The part's pins: the source sits between their nodes
    enum Kind { Thermal, Shot, Channel } kind;
    std::function<double()> psd; // S in A²/Hz at the present operating point (≥ 0)
    QString partId;              // For noiseOff, `noise sources` and the reports
    QString partType;            // Component type (library name), for noiseOff `type:` entries
};
// eElement: new virtual, empty by default
virtual void noiseSources( QList<NoiseSrc>& list ) {}
```

- `eResistor::noiseSources`: when `m_thermalNoise` is set, one Thermal source,
  psd = 4kT·m_admit. It reads the admittance of the moment, so potentiometers,
  LDRs and thermistors follow their value.
- `eDiode`: one Shot source, psd = 2q·|m_current|.
- `eBJT`: two Shot sources: C–E with |I<sub>c</sub>| and B–E with |I<sub>b</sub>|, from the
  converged currents (`COLL->trueCurrent()`, `m_baseCurr`; 0 while NaN).
- `eMosfet`, `eJfet`: one Channel source D–S, psd = 4kT·(2/3)·g<sub>m</sub>.
- `partId` is the id of the Component that owns the element (for an element
  inside a subcircuit, the full id, e.g. `X1-R2`).

### 5.3 Which eResistors are noisy

`eResistor` gets `bool m_thermalNoise = false;` plus `setThermalNoise(bool)`. The
constructors of Resistor, ResistorDip, VarResBase, the Potentiometer's `m_resA` and
`m_resB`, the LDR, the thermistors, the strain gauge and the varistor set it to true.
All other `eResistor` users (meters, sources, battery, lamp, motor, audio out,
volt_reg, eCoil, eReactive) keep false. This is a property of the model family,
not a rule for one part, and new resistor-like parts opt in with one line.

### 5.4 Random numbers

- `std::mt19937_64` with `std::normal_distribution<double>`. There is no Qt RNG,
  and no `rand()` (ds18b20.cpp uses `srand(time)`, which must not disturb the noise).
- One stream per source, seeded with `splitmix64( seed ^ fnv1a64( partId + kind + index ) )`:
  adding or removing one part does not change the noise of the others, and the
  order of the element list does not matter.
- Seed 0 → `seed = std::random_device{}()`, nonzero, stored as "seed in use" and
  reported (§3.5).

### 5.5 eNoise timing

- `initialize()` (called by `startSim()`): Δt = round(1e12 / (2·f<sub>max</sub>)) ps, at least
  1 ps; collect the sources from `m_elementList` except those listed in noiseOff;
  build the streams; set the state to armed. A source is left out when its part's
  label or id, or `type:` plus its component type, is in the list. An entry that
  matches no part gives a warning in Messages and in the CLI note ("noiseOff: no
  part or type matches type:Resistr"), so a typo is not silently ignored.
- `stamp()`: create the pins (§5.1); `addEvent( noiseStart, this )`. noiseStart = 0
  means the first event at 0 (a time of 1 ps is fine: the circuit time starts at 1).
- `runEvent()`: if `noiseStop` > 0 and `circTime ≥ noiseStop`: stamp 0 everywhere,
  set the state to ended, return. Otherwise, for each source:
  i = scale·√(max(psd(),0)·f<sub>max</sub>)·N(0,1); stamp ±i; then `addEvent( Δt, this )`.
- `eNoise` is added to the element list in `startSim()` after `createNodes()` and
  before the initialize loop, only when `noise` is on, and removed at `stopSim()`.
  This keeps the noise-off path identical to today.
- Thread safety: `runEvent()` runs on the simulation thread like every element.
  The status label reads only atomics (state, active).

### 5.6 Settings storage

- `Simulator` gets a `NoiseSettings m_noise` struct with getters and setters.
  `startSim()` copies it into eNoise. A change while running applies at the next
  start, and the CLI note says so ("applies at the next sim start").
- `Circuit::circuitHeader()` writes the attributes **only when `noise` is on or
  a value differs from its default**, so files without noise stay byte-identical.
  `Circuit::loadStrDoc()` parses them in the same `<circuit` loop as `stepSize`.
  `noiseOff` is written comma-separated (part labels, ids and `type:` entries
  cannot contain a comma).
- The Settings dialog (`src/gui/dialogs/appdialog.ui`, "Simulation" page) gets a
  "Noise" group box: an on checkbox, BW (value + unit), scale, seed, a window
  preset combo (Start-up only / Whole run / Custom) with start and stop fields,
  and a parts-off line edit.

## 6. File format

```xml
<circuit version="1.1.0" rev="…" stepSize="…" stepsPS="…" NLsteps="…" reaStep="…" animate="0"
         noise="1" noiseBW="10000000" noiseScale="1" noiseSeed="0"
         noiseStart="0" noiseStop="1000000000" noiseOff="R3,type:Potentiometer" >
```

The times are in ps and the bandwidth in Hz, like the existing attributes. Older
SimulIDE versions ignore unknown attributes, so the files stay compatible.

## 7. CLI (docs/CLI.md)

| Command | |
|---|---|
| `settings set noise=on noiseBW=10MHz noiseStop=1ms` | the §3.1 settings, in the simulation group; values with units are parsed as for `reactStep` |
| `settings get noiseSeed` | the given seed. `sim status` has the seed in use |
| `sim status` | the `noise` field (§3.5) |
| `noise sources` | the list of §3.5 item 7 |

## 8. Implementation steps

All work happens on the branch `feature/transient-noise` in `SimulIDE/`, one
commit per step, and only when the user says so. Each step ends with
`tests/regress/run.sh` all green, plus the new tests of that step.

1. **Settings without sources.**
   - What: `NoiseSettings` in Simulator; header save and load; the CLI
     `settings` rows; the `noise` field in `sim status`.
   - Tests: `test_noise_off_identical` (an RC and a BJT amplifier, noise off: the
     same `sim average` values as on main, and the saved file has no `noise`
     attribute); `test_noise_roundtrip` (set every setting, save, reopen, read
     back).
2. **The eNoise element, the RNG and resistor thermal noise.**
   - What: `noise.h` / `noise.cpp`; the `noiseSources()` virtual; the eResistor
     flag and the opt-in parts (§5.3); `noise sources`.
   - Tests: `test_noise_ktc`: 1 kΩ ∥ 1 nF, BW 100 MHz, reactStep 5 ns, seed 1,
     `sim average` over 2 ms: v<sub>rms</sub> = √(kT/C) = 2.03 µV ± 10 %, with ×1 and
     ×100 (203 µV, the scale check). `test_noise_thermal_divider`: two resistors,
     `noise sources` lists 2 thermal sources with S = 4kT/R.
3. **Junction and channel noise.**
   - What: eDiode, eBJT, eMosfet, eJfet sources.
   - Tests: `test_noise_diode_ktc`: a diode biased by a current source, with C
     across it. The shot noise through r<sub>d</sub> = nV<sub>t</sub>/I gives
     v² = n·kT/(2C) for any I; check at 100 µA and 1 mA, ± 15 %.
     `test_noise_bjt_shot`: `noise sources` reports S = 2q·I<sub>c</sub> at the solved I<sub>c</sub>.
4. **Window, seed and opt-out.**
   - Tests: `test_noise_seed` (seed 7 twice gives identical `sim average`; seed 8
     differs; seed 0 reports a nonzero seed in `sim status`, and that seed repeats
     the run); `test_noise_window` (noiseStop = 1 ms: from 1.1 ms on, the RMS is
     exactly the noise-free value and `noise.active` is false);
     `test_noise_off_list` (noiseOff=R1 removes R1's source; `type:Resistor`
     removes every resistor's source and keeps the diode's; `type:Resistr`
     removes nothing and gives the warning).
5. **The start-up case** (the reason for the feature).
   - Test: `test_noise_multivibrator_start`: a fully symmetric astable
     multivibrator (identical BJTs, R and C). Noise off: check whether it stays at
     balance, and record the result as the baseline. Noise on, window 0–1 ms:
     it oscillates after the window, with the period 1.386·R·C ± 5 %. A symmetric
     bistable: noise on picks a state, and the state depends on the seed.
   - Example circuit: `tests/circuits/noise_multivibrator_start.sim1`.
6. **GUI.**
   - What: the status label; the Messages start line and the reactStep warning;
     the Settings dialog group box; the optional NOISE tag on the oscilloscope
     and plotter.
   - Check: by hand in the visible GUI (COLLABORATION.md), with `capture` PNGs
     attached to the PR.
7. **Measurement replies and docs.**
   - What: `"noise"` in the replies of `sim run`, `sim average`, `scope data`,
     `probe` and `meter`; a section in `docs/CLI.md`; `docs/NOISE.md` for users
     (what makes noise, the formulas, the cost, the sinc roll-off, the parts
     without noise).
   - Tests: the sweeps `roundtrip` and `garble` include the new header
     attributes; `test_noise_reply_tag`.
8. **Performance note.** Measure the real speed of the step 5 multivibrator with
   noise off, the 1 ms window and the whole run at 1 and 10 MHz, and put the
   numbers in `docs/NOISE.md`.

## 9. Acceptance criteria

- With noise off, every existing regression test passes unchanged and saved files
  are byte-identical.
- The levels match within ±10 % (±15 % for shot noise): √(kT/C), the scale law
  and the diode n·kT/(2C).
- The same seed gives an identical run. A random seed is always reported.
- After `noiseStop` the circuit is noise-free and the noise element schedules no
  more events.
- Noise being on is visible in the status bar, the Messages pane, `sim status`
  and every measurement reply taken while it was active.
- The symmetric multivibrator starts with noise and runs at its period.

## 10. Phase 2: the create-circuit skill (separate repo, after phase 1)

- `cc.py run`, `wave` and `average` print a line `NOISE ON ×1 · 10 MHz · seed N ·
  0–1 ms` whenever the reply has a `noise` field.
- `cc.py finish` writes the noise settings into the circuit description, and
  measured values are tagged `[SIMULATED, noise 0–1 ms]`.
- Whether the skill switches on start-up noise by default for oscillators and
  bistables (and thereby closes `lim-osc`, #86–88) is a separate decision, to be
  tested on a regression set. It is a generic simulator setting, not an answer
  for a tested circuit, so the benchmark guardrail allows it.

## 11. Phase 3 (later): the noise source part

A visible part with two pins, a voltage or current source, in the Sources group:
white (rms, bandwidth), 1/f (exponent, level at 1 Hz), random telegraph (amplitude,
mean capture and emission times), mains hum (50/60 Hz plus harmonics) and supply
ripple. It uses the same RNG and seed and the same `noise` visibility. Op-amp input
noise can then be modelled with two of these sources at the inputs. Flicker noise of
the parts themselves would use the PSD shape hook of §5.2.

## 12. Open points for review

- The default bandwidth: 10 MHz is a guess that fits audio circuits and 555
  circuits. Spectre has no default, so the user must choose one.
- The start-up preset of 1 ms is a guess too. A 1 Hz relaxation oscillator may need
  a longer window, or a scale above 1 to start sooner: nV-level noise needs about
  ln(1 V / 1 nV) ≈ 21 growth time constants to reach full swing.

## 13. Sources

- Cadence Spectre transient noise options: [Cadence forum: Transient analyses](https://community.cadence.com/cadence_technology_forums/f/custom-ic-design/34456/transient-analyses), [noise seed](https://community.cadence.com/cadence_technology_forums/f/custom-ic-design/29170/transient-noise-analysis---random-noise-seed), [Spectre/APS block-based transient noise](https://wyz569.github.io/virtuoso/doc/ams_dms_simug/Spectre_or_APS_Block-Based_Transient_Noise_Analysis.html)
- [SIMetrix: Real Time Noise Analysis](https://help.simetrix.co.uk/8.1/simetrix/simulator_reference/topics/commandreference_realtimenoiseanalysis.htm)
- ngspice: [independent sources (TRNOISE, TRRANDOM)](https://nmg.gitlab.io/ngspice-manual/voltageandcurrentsources/independentsourcesforvoltageorcurrent.html), [transient-noise examples](https://sources.debian.org/src/ngspice/26-1.1/examples/transient-noise/rts-1.cir)
- LTspice start-up practice: [Elektor: kickstart a multivibrator](https://www.elektormagazine.com/labs/kickstart-a-multivibrator-in-ltspice), [EasyEDA: initial conditions and starting up circuits](https://docs.easyeda.com/en/Simulation/Chapter9-Initial-conditions-and-starting-up-circuits/)
- The formulas 4kT/R, 2qI, 4kT·(2/3)·g<sub>m</sub> and kT/C are the standard ones (e.g. Horowitz & Hill, *The Art of Electronics*, 3rd ed., ch. 8, in `books/`)
