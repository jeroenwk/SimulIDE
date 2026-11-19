# Spec: fuse

Status: **implemented**, merged into main (2026-11-19), see § 12 for where the code
differs from this text and for the test and benchmark results. Decided with the user on 2026-11-19: a heat
model with two stages and presets, the one-shot fuse first (the resettable PTC fuse right after,
on the same heat code), no arc model in the first version, presets for the IEC speed classes,
and a fuse that must not slow the simulation down noticeably, with a "behave as resistor" mode
for what can't be avoided. Target: SimulIDE 1.1.0-SR2 (this fork).
Measurement and fit scripts: [`fuse/`](fuse/).

## 1. Goal

A **Fuse** part that blows the way a real miniature fuse does:

- it never blows at its rated current, and blows sooner the further it is overloaded, following
  the datasheet's time–current limits;
- a short pulse (switch-on inrush, a capacitor charging) blows it when the pulse's ∫I²dt reaches
  the fuse's **melting I²t**, which is how real fuses are chosen for inrush;
- repeated pulses add up, and the fuse cools between them;
- once blown it stays open until it is replaced, and the moment it blew is logged;
- it costs no more simulation time than a resistor in the same place.

Use cases in this repo: a mains input with a bridge and a reservoir capacitor (will a T 1 A
survive the inrush?), a repair log where a fuse blew (led_driver, slidymoove), and the book
circuits.

### Out of scope for the first version
- Arc after melting (arc voltage, clearing I²t > melting I²t). The fuse opens instantly, so with
  an inductive load the voltage spike is larger than real (§ 9).
- Resistance rising as the element heats (§ 6.4: it would cost a refactorization per change).
- Ambient-temperature derating, UL 248 / automotive types, thermal fuses.
- FF, M and TT presets (the first version has F, T and Custom; § 3.2).
- The resettable PTC fuse: next, as its own part on the same heat code (§ 10).

## 2. What exists today

- No fuse.
- **LED** `MaxCurrent`: shows overcurrent in red and never opens.
- **Lamp** (`outputs/lamp.cpp`): a filament heat model that switches off with `RatedPower = 0`
  (fixed resistance). It updates the filament with a **polling event every 20 µs**
  (`m_thStep`), which § 6 shows to be the expensive way.
- **Meter** (`meters/meter.cpp`): integrates its reading at each converged step that changed its
  pins (`changeCallBack` → `voltChanged()` → `addToSettled` → `stepSettled()`), with no events.
  This is the pattern the fuse follows.
- **Switch** (`switches/mech_contact.cpp`): open = `setAdmit(0)`, closed = `setAdmit(1e3)`.
- Part states ([../docs/PART_STATES.md](../docs/PART_STATES.md)): `reportState(text, level)`,
  badge, outline colour, Messages and `cli status`.

## 3. Properties

### 3.1 The part

| Group | Property | Unit | Default | Notes |
|---|---|---|---|---|
| Main | `Current` (rated current Iₙ) | A | 1 | |
| Main | `Speed` | F / T / Custom | T | IEC 60127 characteristic; sets the heat-model constants (§ 4.3) |
| Main | `I2t` (melting I²t) | A²s | 0 = from the preset | Datasheet "melting I²t"; overrides the preset |
| Main | `Vdrop` (voltage drop at Iₙ) | mV | 0 = from the preset | Sets the resistance R = Vdrop / Iₙ |
| Main | `Model` | Fuse / Resistor | Fuse | Resistor = behave as a plain resistor (§ 6.3) |
| Ratings | `Voltage` (rated voltage) | V | 250 | Warning only (§ 5.4) |
| Ratings | `Breaking` (breaking capacity) | A | 35 | Warning only (§ 5.4); 35 A = glass "L" |
| State | `Blown` | bool | false | Writable: `false` replaces the fuse (§ 5.3) |
| State | `KeepBlown` | bool | false | A blown fuse stays blown after a stop and restart |
| Custom | `MinFuse`, `Share`, `Tau2` | ×Iₙ, –, s | from F | Used only with `Speed = Custom` (§ 4.3, § 12) |

Value label: `T1A` / `F500mA` (the marking on the cap), like the real part.

### 3.2 Real-fuse numbers behind the defaults [DOC]

From the Schurter 5×20 mm datasheets (sources § 11). Pre-arcing time limits, 0.125–6.3 A ratings
(the datasheets cite IEC 60127-2 sheets 1 (F) and 3 (T)):

| Multiple of Iₙ | 1.5 (min) | 2.1 (max) | 2.75 | 4 | 10 |
|---|---|---|---|---|---|
| **F** (FSF) | 60 min | 30 min | 50 ms – 2 s | 10 – 300 ms | ≤ 20 ms |
| **T** (FST) | 60 min | 120 s | 600 ms – 10 s | 150 ms – 3 s | 20 – 300 ms |

Per rating, typical values:

| Rating | F: voltage drop at Iₙ | F: melting I²t | T: voltage drop at Iₙ | T: melting I²t |
|---|---|---|---|---|
| 100 mA | 840 mV | 0.0014 A²s | 565 mV | 0.107 A²s |
| 500 mA | 150 mV | 0.151 | 160 mV | 3 |
| 1 A | 110 mV | 1.13 | 60 mV | 3.3 |
| 2 A | 90 mV | 5.28 | 60 mV | 16 |
| 4 A | 90 mV | 25.6 | 60 mV | 68.3 |
| 10 A | 70 mV | 183 | 60 mV | 520 |

I²t per Iₙ² is not constant (F: 0.14 at 100 mA, 1.1 at 1 A, 1.8 at 10 A), so the presets use
**a table** of these ratings (all rows of both datasheets), interpolated in log–log for ratings
in between, and the README says to enter the datasheet value of the actual fuse when inrush
matters. Rated voltage 250 V; breaking capacity 35 A (both, up to 3.15 A; 10 × Iₙ above).

## 4. Heat model

### 4.1 Equations

Two heat stages, the element and the body (end caps, glass, holder), in normalized units where
the element melts at θ = 1 and the input is I² (A²):

```
C1 · dθ/dt = I² − G1 · (θ − φ)          element
C2 · dφ/dt = G1 · (θ − φ) − G2 · φ      body → ambient
blows when θ ≥ 1
```

- **Short pulse (adiabatic):** θ ≈ ∫I²dt / C1, so C1 ≈ the melting I²t.
- **Steady current:** θ = I² · (1/G1 + 1/G2), so the minimum fusing current is
  I_mf = 1 / √(1/G1 + 1/G2). Below it the fuse never blows.
- **In between:** the element heats with τ1 = C1 / G1, then the body slowly raises its floor,
  τ2 ≈ C2 / G2. This is what makes a fuse at 2.1 × Iₙ take minutes rather than a second.

The current is taken as constant between two simulator solves, which is exactly the solution the
simulator itself has, so the model is integrated **exactly** over each interval (closed form, a
sum of two exponentials). No time step of its own.

### 4.2 Why two stages (checked, `fuse/fuse_fit.py`)

Both models were fitted to the 1 A limits of § 3.2 plus the datasheet I²t, with
1.6 ≤ I_mf / Iₙ ≤ 2.0:

| | F, 1 stage | F, 2 stages | T, 1 stage | T, 2 stages |
|---|---|---|---|---|
| All IEC limits (1.5 … 10 × Iₙ) | pass | pass | pass | pass |
| I²t at 10 × Iₙ (datasheet 1.13 / 3.3 A²s) | 1.47 (+30 %) | 1.08 (−4 %) | 4.3 (+30 %) | 3.57 (+8 %) |
| Blow time at 2.1 × Iₙ | 0.86 s | 600 s | 2.5 s | 39 s |
| Blow time at 2.75 × Iₙ | 0.27 s | 0.30 s | 0.79 s | 2.56 s |
| Blow time at 10 × Iₙ | 14.7 ms | 10.8 ms | 43 ms | 35.7 ms |

One stage passes the limits (they are wide), but gets I²t, the number that decides inrush,
30 % wrong, and blows in about a second at 2.1 × Iₙ where real fuses take minutes. Two stages fix
both, at the cost of a second exponential.

### 4.3 Presets [INFERRED from the fit]

Stored per speed class, normalized to Iₙ so that one set serves every rating (G scales with Iₙ²,
C1 with the rating's I²t):

| Speed | C1 / I²t | I_mf / Iₙ | Share G2/G1 | τ1 (at the preset I²t) | τ2 |
|---|---|---|---|---|---|
| F | 0.93 | 1.60 | 0.69 | 167 ms | 1740 s |
| T | 1.04 | 2.00 | 0.97 | 421 ms | 22 s |

With a user I²t, τ1 scales with it (C1 changes, the G's don't). `Custom` exposes I_mf, τ1, τ2 and
the share. The fit script regenerates these numbers; the implementation takes them as constants
with a `[INFERRED fuse_fit.py]` comment.

## 5. Behaviour

### 5.1 States (part-state mechanism)

| State | Level | When |
|---|---|---|
| `OK` | on (green) | θ below the steady heat of Iₙ (back from `HOT` below 90 % of it) |
| `HOT` | wait (amber) | θ ≥ θss(Iₙ) = Iₙ²·(1/G1 + 1/G2): the element is hotter than its rated current would keep it (0.25 for T, 0.39 for F). From the heat only, never from the instantaneous current (§ 12) |
| `BLOWN` | fault (red) | it blew; Messages: `F1 blew at 1.234 s (I = 4.1 A, I²t used 3.3 A²s)` |
| `BLOWN, BROKE` | fault | the current at that moment exceeded `Breaking` (§ 5.4) |

`read F1` returns `current`, `voltage`, `heat` (θ in %), `i2t` (∫I²dt since start), `state`
and `blown_at` (simulated time, or null).

### 5.2 Blowing

At θ = 1 the fuse sets its admittance to 0 (as an open Switch) and redraws. That costs one
refactorization, once. The current through the open fuse is 0. With an inductive load the
voltage across it spikes as high as the circuit allows: real fuses arc and limit this (out of
scope, § 1, noted in the README).

### 5.3 Replacing

- A blown fuse stays blown while the simulation runs or is paused.
- **Right-click → Replace fuse**, or `comp set F1 Blown=false`, puts in a fresh, cold fuse
  (θ = φ = 0), also while running.
- **Stop and restart** gives a fresh fuse, unless `KeepBlown` is on (then `Blown` is saved in the
  `.sim1` too, so a circuit can be shared "as found").
- `Blown=true` blows it by hand (to show what a circuit does with an open fuse).

### 5.4 Ratings (warnings, no effect on the circuit)

- **Breaking capacity:** if \|I\| at the moment of blowing exceeds `Breaking`, the state becomes
  `BLOWN, BROKE` with a Messages line: a real glass fuse may shatter or keep arcing.
- **Rated voltage:** if \|V\| across a blown fuse exceeds the peak of `Voltage` (an AC rms
  rating: √2 × 250 V = 354 V), Messages notes it once per run: a real fuse may arc across. (First
  built against 250 V itself, which flagged every blown fuse on 230 V mains, found in the
  led_driver circuits.) The note gives the time it happened and the highest voltage until the
  next frame; within 100 µs of blowing it adds that this is the inductive spike of a current cut
  with no arc (§ 9): a 1 Ω short across led_driver's Z1 on the isolation-box model cuts 76 A and
  gives an 87.6 kV one-step spike.

### 5.5 Drawing

IEC symbol: a rectangle with a line through it (the element). `HOT`: the element drawn in amber,
thicker as θ rises (only with part states shown). `BLOWN`: the element drawn broken (a gap in the
middle) in red. Value label `T1A`.

## 6. Performance

Requirement (user): adding a fuse must not slow the simulation down significantly; where that
is unavoidable, there must be a "behave as resistor" option.

### 6.1 Measured (2026-11-19, `fuse/fuse_bench3.py`, headless, speed unlimited, best of 2×3 runs)

The fuse does not exist yet, so its two possible designs were measured with stand-ins in the
place the fuse would go: a 0.1 Ω **Resistor** (any two-pin part: one more node), a Resistor with
a **Voltmeter** across it reading RMS (a callback and an integration on every step that changes
the nodes: the design of § 6.2), and a **Lamp** with its heat model on (a polling event every
20 µs: the obvious design). Wall time, change against a plain wire:

| Circuit (simulated span) | Resistor | Resistor + per-step callback | Lamp, heat model polling 20 µs |
|---|---|---|---|
| quiet: battery + load, DC (100 s) | +1 % | −1 % | **+154 %** |
| rect50: 50 Hz sine, diode, 1000 µF, 47 Ω — 6 nodes (20 s) | +13 % | +21 % | +30 % |
| sw100k: 100 kHz square, diode, 10 µF — 7 nodes (2 s) | +11 % | +11 % | +23 % |
| rect50 + 30-section ladder — 36 nodes (10 s) | −1 % | 0 % | +7 % |
| sw100k + 30-section ladder — 37 nodes (1 s) | 0 % | 0 % | 0 % |

Noise between runs is about ±3 %. What this shows:

1. **The extra node** costs 11–13 % in a 6-node circuit and nothing measurable in a 36-node one.
   Every two-pin part pays it, including a fuse in resistor mode; the only way round it would be
   merging the two nodes while the fuse is intact, which SimulIDE's solver can't do. It is the
   same cost as putting a 0.1 Ω resistor there, which is acceptable.
2. **A callback per changed step** adds up to about 8 % in the smallest switching circuit and
   nothing in realistic ones.
3. **Polling** is the expensive design: it multiplies the time of a quiet circuit by 2.5 (5 M
   events per 100 s for nothing), and costs 7–12 % in the busy ones. The fuse must not poll.

### 6.2 Design rules (what keeps it at the cost of a resistor)

- **No events of its own in normal operation.** The fuse registers `changeCallBack` on its two
  pins (like Meter and Lamp). A linear element's `voltChanged()` runs once per converged solution
  (`Simulator::solveCircuit`, after the non-linear loop), so the fuse sees each new current
  exactly once, at its exact time.
- **Cheap per change:** `voltChanged()` only does
  `S += I_prev² · (t − t_prev); t_prev = t; I_prev = (V0 − V1) · G` (a few flops, no `exp`).
- **The exact update ("flush")**, two `exp()` calls, runs only when:
  - the time since the last flush exceeds h = τ1 / 50 (the power averaged over h changes the blow
    time by well under 1 %);
  - the adiabatic bound says it could melt: θ + S / C1 ≥ 1 (cooling only lowers θ, so a melt is
    never missed; from there on every change is flushed, so short pulses are exact);
  - the GUI frame (`updateStep`, for the drawing and `read`);
  - its own blow event (below).
- **One blow event, only under overload.** After a flush, if the steady state with the present
  current is above melting (I > I_mf), the fuse predicts its blow time from the closed form and
  schedules one event there. It reschedules only if a later prediction is *earlier* than the one
  pending; the event flushes and blows, or schedules again. With I ≤ I_mf any pending event is
  cancelled. So a quiet, overloaded circuit (DC fault, nothing else switching) still blows at the
  exact time, with one event instead of 50 000 polls.
- **A quiet circuit costs nothing:** with no changes there are no callbacks and no events;
  cooling is applied in closed form at the next change or frame.
- **No resistance change while intact** (§ 6.4), so the fuse never forces a refactorization
  except once, when it blows.

### 6.3 Resistor mode (`Model = Resistor`)

The fuse behaves exactly as `Resistor` with R = Vdrop / Iₙ: no callbacks, no events, never
blows, no state. It removes the per-step callback (up to 8 % in a tiny switching circuit),
not the extra node. Use it in a circuit where the fuse is only there for completeness and speed
matters. The drawing shows the element dashed so it is clear it can't blow.

### 6.4 Why the resistance stays constant

A real element's resistance rises by roughly 20–50 % near Iₙ. Modelling it means a new
resistance, a re-stamp and a **refactorization of its node group** at every change of θ, which
is the one thing § 6.1 shows must be avoided. The fuse uses its resistance at Iₙ (from the
datasheet voltage drop), which is right where the drop matters most. Later, if wanted: update R
only when it moves by more than 5 %, which would take a few dozen refactorizations per overload.

### 6.5 Acceptance (benchmarked in § 8)

With the fuse in place of the stand-ins of § 6.1:
- Fuse (thermal) against a Resistor in the same place: ≤ +10 % on rect50 / sw100k,
  ≤ +2 % on the 36-node circuits, no change (± noise) on the quiet circuit;
- Fuse in resistor mode against a Resistor: within noise (± 3 %);
- under a DC overload with nothing else switching: at most 2 events from the fuse up to the
  moment it blows.

## 7. Code changes

- `src/simulator/elements/passive/e-fuseheat.{h,cpp}`: `FuseHeat`, the heat model alone (no Qt,
  no circuit): `setParams(C1, G1, C2, G2)`, `advance(dt, I²)` (closed form), `timeToMelt(I²)`,
  `theta()`. Unit-testable, and reused by the PTC fuse.
- `src/gui/circuitwidget/components/switches/fuse.{h,cpp}`: `class Fuse : public Comp2Pin, public
  eResistor`, properties of § 3, presets of § 4.3 (table of § 3.2 for I²t and Vdrop),
  `stamp()` (callbacks unless resistor mode), `voltChanged()`, `runEvent()`, `updateStep()`,
  `paint()`, part states (`enableStates`, `showStateProp`, `reportState`), the context-menu
  action, `read` fields (`cli_measure.cpp`).
- Listed in the **Switches** category, next to Relay (it is a switch that opens on overload):
  `ItemLibrary::loadItems()` with its `#include`, icon `fuse.png`. Both folders already exist, so
  no `INCLUDEPATH` change; rerun `qmake` for the new files.
- Docs: a Fuse section in `docs/MODEL_LIMITS.md` (instant opening, constant R, presets from two
  datasheets).

## 8. Verification

Test circuits in `tests/circuits/fuse_*.sim1`, checks in `tests/fuse/test_fuse.py` (`simctl`,
results [SIMULATED]), and a Fuse section in `tests/cli/smoke.sh`.

1. **Time–current limits:** F and T, 1 A, a current source at 1.5 / 2.1 / 2.75 / 4 / 10 × Iₙ.
   Each blow time falls within its § 3.2 window and within 1 % of `fuse_fit.py`'s value; 1.5 × Iₙ
   does not blow in 1 h.
2. **I²t:** a 10 × Iₙ pulse 5 % shorter than the melting time does not blow; 5 % longer does.
3. **Inrush:** 325 V peak switched on at the crest into a bridge + 470 µF through 1 Ω: F 1 A blows,
   T 1 A survives (from the I²t numbers: checked against a hand calculation in the test).
4. **Accumulation and cooling:** a pulse train at 3 × Iₙ, 10 ms on, with off times of 10 ms
   (blows) and 2 s (never blows within 60 s).
5. **Cooling in a quiet circuit:** heat to θ = 0.9, then 10 s with no change; `read` heat matches
   the closed form within 0.1 %.
6. **AC:** 50 Hz sine at 3 × Iₙ rms: blow time within 5 % of DC at 3 × Iₙ.
7. **Exact time when quiet:** DC 3 × Iₙ, nothing else in the circuit: `blown_at` within 0.1 % of
   the closed form, and at most 2 fuse events (event count from the test build's debug counter or
   `cli status`).
8. **Replace and restart:** `Blown=false` while running restores current; restart gives a fresh
   fuse; with `KeepBlown` it stays blown and saves.
9. **Warnings:** a short from a stiff source (> 35 A) gives `BLOWN, BROKE`; 400 V across a blown
   250 V fuse gives the Messages note.
10. **Resistor mode:** same node voltages as a Resistor of Vdrop / Iₙ at 100 × Iₙ; never blows.
11. **Save / load:** every property round-trips through `.sim1`.
12. **Performance:** `fuse/fuse_bench3.py` extended with a `fuse` and a `fuse-res` kind; the
    acceptance numbers of § 6.5, recorded in § 12. Also `tools/sim_bench.sh` before and after, to
    show that circuits without a fuse are unchanged.

## 9. Known limits (for the README and MODEL_LIMITS)

- Opens instantly: no arc, so an inductive load gives a larger spike than a real fuse, and the
  clearing I²t equals the melting I²t (real: higher).
- Resistance constant (value at Iₙ); a real fuse has less resistance cold.
- Presets from one manufacturer's 5×20 mm datasheets (Schurter FSF, FST), and the heat-model
  constants fitted at 1 A only; other ratings scale with I²t. Real fuses spread widely within the
  IEC windows.
- Ambient temperature, holder and PCB copper (which change real blow times by tens of %) are
  ignored.

## 10. Later

- **Resettable PTC fuse** (polyfuse): `I_hold`, `I_trip`, `R_min`, `R1max`, time to trip at a
  given current, from a datasheet (e.g. Littelfuse / Bourns). Same `FuseHeat`, but on tripping its
  resistance rises steeply (a few refactorizations, limited as in § 6.4) instead of opening, and
  it resets as it cools. Its own spec.
- FF, M, TT presets, each fitted from a datasheet with `fuse_fit.py`.
- Arc model: an arc voltage for the arcing time after melting, as an option.
- Resistance rising with θ (§ 6.4).

## 11. Sources

- Schurter FSF 5×20 (quick-acting F, IEC 60127-2/1) datasheet:
  <https://sy-dep-epc-lpc.web.cern.ch/components/datasheets/epc-lpc%20(converters)/FSF-Series_Fuse_(shurter).pdf>
- Schurter FST 5×20 (time-lag T, IEC 60127-2/3) datasheet, revision 21.07.2026:
  <https://static.maritex.eu/file/display/OP4XDOLIEdVXW8FFHtreIkH_cAYIT7eY/FST_5x20.pdf>
- IEC 60127-2 itself was not read; its limits are taken from the two datasheets above, which
  quote it.

## 12. Implementation notes (2026-11-19)

Code: `src/simulator/elements/passive/e-fuseheat.{h,cpp}` (`FuseHeat`),
`src/gui/circuitwidget/components/switches/fuse.{h,cpp}` (`Fuse`), the `read` fields in
`src/cli/cli_measure.cpp`, `src/icons/components/fuse.png`. Tests: `tests/fuse/test_fuse.py`
(sections selectable: `python3 tests/fuse/test_fuse.py gates pulse ...`) with its RK4 reference
`tests/fuse/fuse_ref.py`; demo circuit `tests/circuits/fuse_states.sim1`.

### Where the code differs from the text

- **Custom** has `MinFuse`, `Share` and `Tau2`; τ1 is not a property but follows from `I2t`
  (C1 = I²t, as the fit gives C1 ≈ I²t). The Custom group is always listed (SimulIDE has no
  per-property hiding); its captions say "(Speed = Custom)".
- **Saving `Blown`:** the property is saved as it is. `KeepBlown` only decides what a restart
  does with a fuse the simulation blew; a fuse blown by hand (`Blown=true`, or a file saved
  with it blown) stays blown at a restart.
- **Preset rows:** the I²t and voltage-drop tables hold every rating of both datasheets
  (log–log interpolation; outside them I²t scales with Iₙ², the drop stays). `Custom` uses the F
  table when `I2t` / `Vdrop` are 0. The breaking capacity preset is max(35 A, 10 × Iₙ).
- **States:** `HOT` is reported from the change that caused it (a linear element's
  `voltChanged()` already runs on the converged solution), not through `stepSettled()`.
- **`HOT` from the heat, not the current (2026-11-19):** the first version also turned `HOT`
  on |I| > Iₙ, so a switching load flooded the Messages: led_driver `board_mains` draws ±1.9 A
  pulses at 85 kHz through its T 1 A fuse (0.18 A rms) and F1 went OK → HOT → OK at every
  pulse. The element follows I² with τ1 (tens of ms), so the state now follows θ only, with the
  thresholds of § 5.1 and no chatter. In a quiet circuit nothing calls `voltChanged()`, so each
  flush plans an event at the next threshold crossing at the present current
  (`FuseHeat::timeToLevel`, bisection like `timeToMelt`), and a change that isn't flushed yet
  schedules a flush within τ1/50, also when an event is already pending further away (an
  overload's melting bound, ~32 ms ahead at 10 × Iₙ for T 1 A: `HOT` came only then, found by
  the smoke test).
- **Property changes during a run** hold the simulation only when it is running: `pauseSim()`
  does nothing on a paused simulation but `resumeSim()` would resume it, which would undo the
  user's pause.
- **Blow-time check without an event counter:** § 8 item 7 is checked by wall time instead
  (below): 700 s of a quiet overload cost the same as the circuit without a fuse.
- **Exact flush rule:** besides τ1/50 and the adiabatic bound (θ_upper > 0.95), an overload
  (I > I_mf) schedules an event at the adiabatic lower bound of the melting time only when the
  current rose above the one the pending event was planned for; the event computes the exact
  melting time (`FuseHeat::timeToMelt`, bisection to 1e-12) and schedules it.

### Tests (`tests/fuse/test_fuse.py`, 60 checks, all pass) [SIMULATED]

| Check | Simulated | Expected |
|---|---|---|
| F 1 A blow times at 2.1 / 2.75 / 4 / 10 × Iₙ | 600.4 s / 0.2969 s / 83.3 ms / 10.84 ms | RK4 reference within 0.2 %; all inside the IEC windows; 1.5 × Iₙ not blown in 1 h |
| T 1 A blow times at 2.1 / 2.75 / 4 / 10 × Iₙ | 38.96 s / 2.562 s / 297.7 ms / 35.69 ms | same |
| T, 10 × Iₙ pulse of 0.95 / 1.05 × the melting time | survives (10.17 % heat 1 s later) / blows | reference |
| Inrush 325 V → 10 Ω + 470 µF | T survives, I²t 2.467 A²s (V²C/2R), heat 45.29 % (ref 45.24 %); F blows | |
| 50 Hz square, 3 × Iₙ at 50 % | blows at 33.79 s | reference 33.79 s; 0.5 % duty never blows |
| Cooling: 0.5 s at 3 × Iₙ, then 10 s quiet | 1.672 % | 1.672 % |
| 50 Hz sine at 3 × Iₙ rms | 0.9166 s | sine reference 0.9165 s, DC at the same rms 0.9268 s |
| States: 3 × Iₙ pulses at 10 % duty, 100 kHz (0.95 Iₙ rms); 1.2 × Iₙ steady; T 1 A at 10 × Iₙ | stay `OK`; `HOT` at 12.23 s; OK, HOT at 8.642 ms, BLOWN | no chatter; reference 12.23 s; reference 8.643 ms |
| Replace while running, restart, KeepBlown, blown by hand, BROKE on a short, the 400 V note, resistor mode at 100 × Iₙ, save/load of every property, label `F500mA` | all as specified | |

### Performance (§ 6.5) — `specs/fuse/fuse_bench3.py`, headless, speed unlimited

Fuse (T 10 A with `Vdrop` 1 V, so 0.1 Ω like the resistor stand-in), best of 4 × 2 runs, change
against a **resistor** in the same place:

| Circuit | Fuse | Fuse, resistor mode | Target |
|---|---|---|---|
| quiet (100 s) | 0 % | +1 % | no change |
| rect50, 6 nodes (20 s) | +4 % | 0 % | ≤ +10 % |
| sw100k, 7 nodes (2 s) | 0 % | 0 % | ≤ +10 % |
| rect50 + ladder, 36 nodes (10 s) | +2 % | +2 % | ≤ +2 % (both read the same: noise, ±3 %) |
| sw100k + ladder, 37 nodes (1 s) | 0 % | 0 % | ≤ +2 % |
| quiet overload, F 1 A at 2.1 × Iₙ for 700 s | 0.177 s wall, blows at 600.033 s | (no fuse: 0.176 s) | ≤ 2 events |

Rerun 2026-11-19 with `HOT` from the heat (best of 4 × 2; rect50 best of 6): quiet −3 %,
rect50 +7 %, sw100k 0 %, rect50 + ladder −1 %, sw100k + ladder 0 %; quiet overload 0.179 s wall
(no fuse 0.177 s), blows at 600.033 s. The rect50 change from +4 % is not the new code: main,
measured the same day, reads +8 % (resistor 1.299 s, fuse 1.404 s), the branch +7 % (1.346 s,
1.446 s); and without the τ1/50 fallback event the branch still reads 1.446 s.

`FuseHeat::advance()` (two exponentials) takes 13 ns and runs at most every τ1/50 of simulated
time outside an overload.
