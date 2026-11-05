# Spec: inductor and transformer current display

Status: **implemented** (2026-11-05) on branch
`feature/inductor-current-display`; decisions in section 9, where the code
differs from the text in section 11. Mockup:
[inductor-current-mockup.png](inductor-current-mockup.png). Companion to
[capacitor-charge.md](capacitor-charge.md), whose section 10 announced it
("Inductors: the gauge shows |I| / Iref, with the same colour logic").
Target: SimulIDE 1.1.0-SR2 (this fork). Points that were open when this was
proposed are marked **[OPEN]**; section 9 records what the user decided.

## 1. Goal

A capacitor stores energy in its voltage (½CV²), an inductor in its current
(½LI²). The capacitor display shows the voltage; this one shows the current,
with the same look, so the two read the same way side by side:

- **Gauge:** the same 3 × 14 px bar beside each inductor, filling with
  |I| / Iref.
- **Transformers:** one gauge per coil (winding section), beside that coil.
- **Label:** optional live text, in amps, percent or both (`63 mA`, `63 %`,
  `63 mA · 63 %`), with the sign of the current.
- **Warning:** an inductor above its rated (saturation) current is flagged in
  red, like a capacitor above its rated voltage.

Together with the capacitor gauges this shows energy moving back and forth:
- **LC tank:** the capacitor gauge empties while the inductor gauge fills, and
  the other way round, a quarter period apart.
- **RL step:** the inductor gauge rises with τ = L/R, while its flow dots
  speed up.
- **Boost / buck:** the inductor current ramps up and down at each switch
  cycle.
- **Transformer:** the secondary current follows the load, and the primary
  current follows the secondary plus the magnetising current.

### Out of scope for the first version

- **A core flux / magnetising-current gauge** on transformers: section 10.
- **Charge-log events** in Messages ("current rising", "at steady state"):
  the EventLog stays capacitor-only.
- **Saturation in the model:** the rating only warns, as `MaxVolt` does. The
  inductance stays linear.
- **Inductors inside subcircuits:** they are not drawn, so they show nothing
  (as for capacitors).

## 2. What exists today

- **`Inductor`** (`components/passive/reactive/inductor.*`): `Reactive` →
  `Comp2Pin` + `eReactive`, like the capacitor. Properties: Inductance,
  Resistance (series), InitVolt (shown as "Initial Current", A), ReaStep.
  `indCurrent()` returns the Norton source `m_curSource`, not the full branch
  current (the relay uses it). Body 20 × 12 px, value label below.
- **`Transformer`** (`transformer.*`): a `Component` + `eElement`, not a
  `Reactive`. Each coil is an `eCoil` (`src/simulator/elements/passive/e-coil.*`)
  between two pins, joined to them through two 1000 S resistors
  (`m_resistor0/1`). Coupled through the inverse inductance matrix
  `m_inva`. Primary on the left (x = −16), secondary on the right (x = +16),
  8 px per turn, pins every coil end; coils sit between x = ±6 and ±14, the
  core at x = ±2. There is no free space between the coils and the core.
- **Pin current:** `ePin::getCurrent()` gives the current into an element
  through a pin, reconstructed from the stamps of the solved step
  ([current-flow.md](current-flow.md) section 3). It is exact for companion
  models, and it is what the flow dots show.
- **Capacitor display:** `ChargeView` (an `Updatable`: settings, `Vref`, blink
  phase), `ChargeGauge` (a child item drawn upright, beside the part, opposite
  the value label), `CapacitorBase::chargeRef()` (own scale or `Vref`).

## 3. What is measured, and the reference (100 %)

### Current

- **Inductor:** `I = ePin::getCurrent()` of pin 0 (`lPin`): positive when the
  current enters at pin 0 and leaves at pin 1. It includes the conductance part
  of the companion model, unlike `indCurrent()`.
- **Transformer coil:** the current through the coil's own pin-0 resistor
  (`m_resistor0`), positive from the coil's first pin to its second. A new
  `eCoil::current()` returns it. A centre-tapped winding shows its two halves
  separately, with their own currents.

### Reference current Iref

Currents have no equivalent of the nominal source voltage that makes `Vref`
work for capacitors. A source voltage says nothing about the current until the
circuit is solved. So the layers are:

1. **Part override:** a `CurrentScale` property (A, 0 = the circuit's) on
   inductors and transformers, like the capacitor's `ChargeScale`.
   `iref_source: part`.
2. **Circuit override:** `currentScale` (A, 0 = auto), saved in the `.sim1`
   header like `chargeScale`; in Settings > Circuit as "100 % at … A" on the
   inductor row. `iref_source: override`.
3. **Auto: the peak |I| of all inductors and coils since the start**, one
   value shared by every gauge so they stay comparable. Reset when the
   simulation starts. `iref_source: peak`.

The auto layer has a known weakness, stated here so nobody is surprised: on
the **first rise** of an RL step the inductor current is its own peak, so the
gauge reads 100 % the whole way up. It works well for anything that repeats
(LC tank after the first quarter period, AC, switching converters after the
first cycles) and for decays. For an RL step, set `currentScale` or the part's
`CurrentScale` (e.g. V/R). **[OPEN]** alternative: keep the peak from the
previous run (reset only when a circuit is opened), so the second run of the
same circuit rises properly; the cost is a stale scale after editing values.

Rejected:
- **The rating as the scale:** gauges would not be comparable (same reason as
  for capacitors). The rating is for the warning.
- **The current-flow scale** (largest wire current, decaying): in an RL loop
  the largest current *is* the inductor's, so it reads 100 % too, and the
  scale falls back by itself within a second, so a constant current would
  slowly read "full".
- **A DC solve for the final current:** that is the "progress towards the
  final value" item, kept for later in both specs.

`level = |I| / Iref`. Above 1 the gauge stays full with the overflow
triangle; the label shows the real figure.

## 4. Rendering

### Gauge

- **The same `ChargeGauge` item and drawing** as for capacitors: 3 × 14 px,
  vertical on screen, fills upwards in `flowColor`, overflow triangle, red
  rating tick, upright text in 7 px.
- **Inductor position:** as for capacitors, 9 px from the centre on the side
  opposite the value label (above a horizontal inductor whose value is below,
  to the right of a vertical one whose value is on the left).
- **Transformer position:** one gauge per coil, **outside** the part, centred
  on the coil, between that coil's two pins (where wires don't run: they
  leave the pins sideways). Primary gauges left of the pin ends with the text
  to their left, secondary gauges right with the text to their right. A coil
  shorter than 16 px (1 turn) gets a gauge as tall as the coil minus 4 px.
  **[OPEN]** for rotated transformers: the bar stays vertical on screen, and
  sits above/below the coil instead.

### Coil colouring (decided: coloured)

The capacitor plates show the potential of each node; the inductor's coil
shows its current: `t = clamp(I / Iref, −1, 1)`, from the line colour (grey)
at 0 to `animHighColor` at +1 (current from pin 0 to pin 1; for a transformer
coil, from its first pin to its second) and `animLowColor` at −1. The same
colours as the capacitor plates and Animate, so the reading is "red = +,
blue = −" on both, with the quantity (potential or current) given by the part.
Transformer coils are coloured one by one; the core stays in the line colour.

### Label

- The shared label setting: `off | volt | percent | both` keeps its name. For
  inductors "volt" means "the value", shown in amps: `63 mA`, `-1.2 A`
  (3 significant digits, engineering prefix, sign shown when negative).
- **Percent:** as for capacitors (`<1 %`, above 100 % on overflow).
- **Transformers:** the label is per coil, beside its gauge. In `both` mode a
  transformer with several coils gets long; **[OPEN]** whether transformers
  show the current only.
- Below 1 µA: `0 A · 0 %`.

### When

Only while the simulation runs or is paused, and only for connected parts, as
for capacitors. When stopped, nothing changes in the drawing.

## 5. Warning

- **New `MaxCurrent` property on the inductor** ("Rated current (0 = not
  set)", A): above it the gauge fill and label turn red and blink at 2 Hz,
  the rating tick sits at `MaxCurrent / Iref`, and `read` gives the flag
  `overcurrent`. It stands for the saturation or thermal rating of a real
  part; the model does not saturate.
- **Transformers:** no rating in this version (one property can't describe
  every coil). Later: a rating per winding.

## 6. Code changes

- **`ChargeGauge` becomes generic.** It draws from a small interface instead
  of reading `CapacitorBase` directly:
  `struct GaugeSource { virtual double gaugeLevel(); virtual bool gaugeWarn();
  virtual double gaugeTick(); virtual QString gaugeText( int mode ); }`, plus
  its position (part centre and side, or an explicit rectangle in part
  coordinates for transformer coils). `CapacitorBase` implements it with no
  change in behaviour.
- **`ChargeView`** gains `iref()` / `irefSource()` (layers of section 3), the
  shared peak of the inductor currents, and `repaintInductors()` next to
  `repaintCaps()`. Possibly renamed `ReactiveView`; the settings keys stay.
- **`Inductor`:** `indCurr()` (full current), `currentLevel()`,
  `currentRef()`, `currentFlags()`; properties `CurrentScale` and
  `MaxCurrent`; one `ChargeGauge`; `updateStep()` refreshes it like
  `CapacitorBase::updateStep()`. `indCurrent()` stays as it is for the relay.
- **`eCoil::current()`**, and **`Transformer`**: one gauge per coil, created
  and placed in `create()` / `modify()` (the coils and pins are rebuilt when
  Primary / Secondary change), refreshed from a new `updateStep()` (the
  transformer is added to the update list), property `CurrentScale`.
- **`Circuit`:** `currentScale` next to `chargeScale` in the `.sim1` header.
- **No change to the solver.** With the display off nothing extra runs.

## 7. Settings, UI and CLI

**[OPEN] One switch or two.** Recommended: **one display, one switch.** The
toolbar button and the Settings checkbox become "Show Capacitor Charge and
Inductor Current" (tooltip: stored energy in capacitors and inductors); the
setting keys `capCharge` and `capLabel` keep their names so existing
`simulide.ini` files and scripts keep working. The alternative is a second
button and `indCurrent` / `indLabel` settings, mirroring the capacitor ones.

- **Settings > Circuit:** on the same row block, a second "100 % at … A"
  box for `currentScale` (Auto = 0), next to the capacitor's "100 % at … V".
- **Part properties:** Inductor: "Current gauge 100 % at (0 = circuit)" (A),
  "Rated current (0 = not set)" (A). Transformer: the same scale property.
- **CLI:**
  - `settings set currentScale 0.5` (A, 0 = auto); `settings get`.
  - `comp set L1 CurrentScale=0.1 MaxCurrent=2`.
  - `read L1`: `current` (signed, A), `level`, `iref`, `iref_source`
    (`part | override | peak | none`), `rating`, `flag` (`overcurrent`,
    `overflow`). Works paused and with the display off.
  - `read T1`: per coil `i_P01`, `level_P01`, `i_S01`, … (the coil names
    from the pin names), plus `iref` and `iref_source`.
- **Docs:** `docs/CLI.md` (settings, read fields, properties),
  `docs/COLLABORATION.md` (one line, next to the capacitor one).

## 8. Verification

Tests go in `tests/cli/smoke.sh`, headless, section "Inductor current
display".

1. **RL step.** 10 V rail, 100 Ω, 1 H (τ = 10 ms), `currentScale` 0.1:
   `iref_source` override; level 0.632 ± 0.01 at 10 ms, 0.993 ± 0.01 at
   50 ms; `current` positive with the inductor's lPin on the supply side and
   negative when flipped.
2. **Auto peak.** The same without a scale: `iref_source` peak, level 1.0
   during the rise (the documented weakness), and after the rail is switched
   off the level decays with τ while `iref` holds 0.1 A.
3. **Part scale.** `CurrentScale=0.2` on L1: `iref` 0.2, `part`, level 0.5
   at steady state; saved and reloaded; 0 falls back to the circuit.
4. **Rating.** `MaxCurrent=0.05` at 0.1 A: flag `overcurrent`, `rating` 0.05.
5. **LC tank.** 1 mH, 10 µF, C InitVolt 10 V (f ≈ 1.59 kHz,
   I_peak = V·√(C/L) = 1 A): at t = T/4 the inductor level ≈ 1 with
   `iref` ≈ 1 A and the capacitor level ≈ 0; at T/2 the other way round.
6. **Transformer.** 1:2 (Secondary=2), 10 V 50 Hz sine primary, 1 kΩ
   secondary load: `i_S01` ≈ 20 V / 1 kΩ in amplitude (peak over a period)
   within 5 %, and `i_P01` ≈ 2 × `i_S01` plus the magnetising current.
7. **Label formats** in a capture (`capLabel` volt / percent / both), and the
   Settings-dialog persistence check, as for capacitors.
8. **Visual, GUI:** the LC tank and the transformer with current flow on:
   gauges move a quarter period apart; nothing overlaps the wires at the
   transformer pins.
9. **Performance:** the 555 fader and a switching converter example, display
   on vs off: `sim status` speed within 5 %.

## 9. Decisions (user, 2026-11-05)

1. **One shared switch** and label setting: `capCharge` / `capLabel` drive
   both displays; the button reads "Show Capacitor Charge and Inductor
   Current".
2. **Reference as for capacitors:** a shared value by default (the circuit's
   `currentScale`, Auto = the shared peak since the start), which each part can
   override with its own `CurrentScale`.
3. **The coil is coloured** by its current (section 4 option): grey at 0, the
   Animate high colour at +Iref (current from pin 0 to pin 1, or from a
   transformer coil's first pin to its second), the low colour at −Iref.
4. **Transformer labels follow the label mode**, unless that makes them
   cluttered. It doesn't: coils are never shorter than 3 turns (24 px), so the
   labels never collide (section 11), and no exception was needed.
5. **Rotated transformers:** gauges above / below the coils (as proposed).

## 10. Later

- **Core gauge on transformers:** the magnetising current, Σ (turns ratio ×
  coil current) referred to the primary, as one gauge between the windings or
  above the core. With a saturation current it would show a transformer
  driven into saturation (DC on the primary, too low a frequency).
- **Ratings per winding**, and a saturating inductor model.
- **Charge-log events** for inductors in Messages.
- **Progress towards the final value** (DC operating point), shared with the
  capacitor display.
- **Energy label** ½LI² next to ½CV².

## 11. Implementation notes (2026-11-05)

Where the code differs from, or adds to, the text above:

- **`ChargeGauge` moved to `src/gui/circuitwidget/chargegauge.*`** with the
  `GaugeOwner` interface (`gaugeLevel`, `gaugeWarn`, `gaugeTick`,
  `gaugeText`). `CapacitorBase`, `Inductor` and a `CoilGauge` per transformer
  coil implement it. `Component::valLabel()` gives the gauge the value label.
- **Inductor current** is the current of the part's series resistor
  (`Reactive::seriesCurrent()`), which is the branch current of the companion
  model, rather than `ePin::getCurrent()`: the same value, and it needs no
  stamp reconstruction. **Coil current** is `eCoil::current()`, the coil's
  pin-0 coupling resistor (1000 S).
- **`iref_source`:** `part`, `override`, `peak` or `none` (no current yet).
  Capacitors keep `capacitor` for their own scale.
- **The shared peak leaves out parts with their own `CurrentScale`**: they
  opted out of the shared ruler, and a large current there would otherwise
  shrink every other gauge.
- **Initial values count in both peaks.** The peak is sampled once per frame
  (and on `read`), and a short run can end between two frames: an LC tank
  started from a capacitor's InitVolt read `vref` = the voltage at the first
  sample, so the capacitor showed "overflow" at 0.09 V. The capacitor peak now
  also takes each capacitor's |InitVolt|, the current peak each inductor's
  initial current.
- **Below 1 nA a current counts as 0** for the level, the colour and the
  peak (`ChargeView::cleanAmps`). An unconnected transformer coil carried
  4·10⁻²² A of rounding noise, which became the shared peak and drew a full red
  coil next to a "0 A · 0 %" label. The label shows nA, so it never reads
  "0 A" for a current that counts.
- **Parts inside parts show nothing:** the relay's coil is an `Inductor`
  child of the relay; its gauge landed on the relay's contact, and the relay
  already shows its state (ENERGISED). Same rule as capacitors inside
  subcircuits.
- **Transformer coils are at least 3 turns** (the longer winding gets 3 turns
  per coil, the other is stretched to the same length), so the labels on one
  side are ≥ 24 px apart and never overlap, even with six coils.
- **`Transformer` is now on the update list** (`updateStep()` refreshes its
  gauges and coil colours while the display is shown; otherwise it only checks
  whether a gauge is still visible).

### Tests (`tests/cli/smoke.sh`, section "Inductor current display")

Automated, all passing (237 checks in the whole suite):
- RL step: 63.2 mA at τ, the auto peak at 100 % on the rise (test 2), the
  circuit scale 63 % at τ and 99 % at 5τ (test 1), a negative scale
  rejected;
- the part scale and rating: `part`, 50 %, `overcurrent`, `rating`; saved
  and reloaded with the circuit scale; 0 falls back (tests 3, 4);
- LC tank: 1 A in the inductor and an empty capacitor at T/4, the other way
  round at T/2, `vref` 10 V from InitVolt (test 5);
- transformer 1:2 into 1 kΩ: −19.8 mA in the secondary at the sine peak, the
  primary above twice that (reflected load plus magnetising current), its own
  scale used for both coils (test 6).

Checked by hand (captures): coil colours and gauges on the RL, LC and
transformer circuits, a six-coil and a rotated transformer, the relay, the
display gone after a stop. Performance (test 9): `RLC_Resonator` and
`relay-8bit_ALU` at 100 % speed with the display on and off.
