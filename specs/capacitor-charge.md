# Spec: capacitor charge display

Status: **implemented** on branch `feature/capacitor-charge` (2026-11-03),
see section 11 for where the code differs from this text. Decided with the
user on 2026-11-03: the gauge bar beside the part (variant D in
[capacitor-charge-mockups.png](capacitor-charge-mockups.png)), plates
coloured by node potential, a layered voltage reference (section 3), its own
toolbar button, and a live label that is configurable and off by default.
Target: SimulIDE 1.1.0-SR2 (this fork). Companion to
[current-flow.md](current-flow.md).

## 1. Goal

While the simulation runs, let the user *see* each capacitor charge and
discharge, without a scope probe on it:

- **Gauge:** a small bar beside the part fills up as the voltage across it
  rises, and empties as it falls.
- **Plates:** each plate is coloured by the potential of its node, so it shows
  which side is high and which way the part is charged.
- **Label:** optional live text, in volts, percent or both (`19.6 V`, `98 %`,
  `19.6 V · 98 %`). Off by default.
- **Warnings:** a capacitor above its rated voltage, or an electrolytic charged
  backwards, is flagged in red.

The current-flow dots show charge *moving* into a capacitor. This adds the
*state*: how much has arrived. Together they show an RC charge the way a
textbook draws it: fast dots and a quickly rising gauge at first, then slowing
dots and a gauge creeping towards full.

Motivating case: led_driver plan stage R a (`led_driver/sim/stage_Ra_vdd_dsen.sim1`).
- C5 should visibly fill to ≈ 98 % with τ = 22 ms.
- C3, beside it at 0.8 V, should stay nearly empty at ≈ 4 %.
- After switch-off, C5 should stay full while the divider side empties.

### Out of scope for the first version

- **Progress towards the final value** (`→ 19.6 V · 63 %`): this needs a DC
  solve (section 10).
- **Inductors** (gauge = current) and **batteries**: same mechanism, later.
- **Colouring wires by voltage** on a continuous scale: the existing Animate
  mode stays as it is. Only the capacitor plates are coloured here.
- **Charge Q and energy ½CV²** as label formats.

## 2. What exists today

- **Voltage:** `eReactive` (`src/simulator/elements/passive/e-reactive.*`)
  keeps `m_volt`, the voltage across the part at the last reactive step.
- **Classes:** the GUI classes `Capacitor` and `elCapacitor`
  (`components/passive/reactive/`) derive from `CapacitorBase` → `Reactive` →
  `Comp2Pin` + `eReactive`.
- **Per-frame hook:** `Reactive::updateStep()` runs once per frame (the part is
  on the update list) and calls `update()`.
- **Reverse detection:** `elCapacitor::updateStep()` already computes
  `V = V(pin0) − V(pin1)` and sets `m_crashed` when it is below −1 µV.
  `Component::paint` draws crashed parts in red.
- **No voltage rating:** capacitors have Capacitance, Resistance (ESR),
  InitVolt and ReaStep, but nothing to compare the voltage with.
- **Geometry:** the body is 20 × 16 px (`m_area`); the plates are 12–14 px
  long, 6 px apart.
- **Animate colours:** in the Dark theme, `animHighColor` is `#ff6060` and
  `animLowColor` is `#6f8fff`. The flow dots use `flowColor` (`#b9e3b0` in Dark,
  amber in Classic).

## 3. The reference voltage (100 %)

A percentage can answer two different questions:

- **(a) How high is this voltage**, compared with the rest of the circuit?
- **(b) How far along is this charge**, compared with where it is heading?

This version answers (a). One reference, `Vref`, is shared by every gauge and by
the plate colours, so capacitors can be compared at a glance. It is chosen in
layers:

1. **Override:** the circuit property `ChargeScale` (V, default 0 = auto),
   saved in the `.sim1` header like `animate`. Use it for a fixed ruler, e.g.
   400 V for every mains circuit.
2. **Default: the largest nominal source voltage** in the circuit, read from
   the source settings, not from the solution:
   - Fixed Voltage, Rail, Battery and Voltage Source: their voltage.
   - WaveGen: |offset| + amplitude.
   - Sources inside subcircuits count too (e.g. a board's 5 V).
   - A source that is switched off still counts, so the ruler doesn't move
     when the supply is turned off (stage R a switch-off test).
   - Each source exposes it through a new virtual `double Component::nominalVolt()`
     (default 0).
3. **Fallback**, when there is no source (a circuit that only runs on a
   capacitor's InitVolt): the highest |node voltage| since the start, with a
   peak hold that is reset on start.

Why not the other options:

- **Relative to the largest capacitor voltage**, like the current-flow scale: a
  lone capacitor would always read 100 %, even while charging. The motion of
  the gauge is the whole point.
- **Peak hold of all node voltages:** a short spike (e.g. an inductive kick of
  500 V) would make every capacitor look empty for the rest of the run. Nominal
  source values can't spike.
- **Per-capacitor rating as the scale:** gauges would no longer be comparable
  with each other or with the plate colours. The rating is used for the
  warning only (section 5).

**Per-capacitor override (opt-in).** A capacitor's own `ChargeScale` property
(V, default 0 = the circuit's `Vref`; "Charge gauge 100 % at" in its
Properties) replaces `Vref` for that capacitor only: its gauge, % label,
rating tick and plate colours (`CapacitorBase::chargeRef()`). It is for a cap
that lives at a very different level from the largest source, e.g. a 5 V
decoupling cap in a 400 V circuit. The cost is the one above: that gauge is no
longer comparable with the others, and its plates may be coloured differently
from another part on the same node. Left at 0, nothing changes. `read` reports
it as `vref_source: capacitor`. The event log ignores it (its thresholds come
from each capacitor's own peak).

`level = |V| / Vref`. Above 1 (a boost converter, a voltage doubler) the gauge
stays full and shows an overflow mark; the label shows the real figure, e.g.
`112 %`. The reference is computed when the simulation starts and again when a
source property changes, not on every frame.

## 4. Rendering

### Gauge bar (variant D)

- **Shape:** 3 × 14 px (scene units), with a thin outline in the theme's line
  colour at 60 %. It fills from the bottom up: `height = min(level, 1) × 14`.
- **Orientation:** always vertical on screen and filling upwards, like a
  thermometer, whatever the part's rotation. Gauges in a circuit then all read
  the same way.
- **Position:** beside the body, perpendicular to the leads: to the right of a
  vertical part, above a horizontal one, 9 px from the centre. It sits on the
  side opposite the value label, and flips with it if the user moves that
  label (so the gauge never sits on the text).
- **Fill colour:** `flowColor`. The dots carry charge in, and the gauge shows
  it collecting. The colour is the same for both polarities: the plates and the
  label show the sign.
- **Overflow** (level > 1): a small filled triangle on top of the bar.
- **Rating tick:** when `MaxVolt` is set and below `Vref`, a 1 px line across
  the bar at `MaxVolt / Vref`. You can see the margin before anything goes red.
- **Repaint:** `update()` only when the drawn height changes by ≥ 0.25 px, or a
  colour, flag or label changes.

### Plate colours

- Each plate takes the potential of its own node:
  `t = clamp(V(node) / Vref, −1, 1)`.
- The colour runs from the line colour (grey) at 0 to `animHighColor` at +1,
  and to `animLowColor` at −1. These are the same colours Animate uses, so
  the two displays agree.
- Example in R a: C5's top plate turns red as VDD rises, and its bottom plate
  (pin 8, 0 V) stays grey.
- The plates keep their normal shape and width: only the pen colour changes.

### Label

- Setting `capLabel`: `off | volt | percent | both`, default `off`.
- **Volts:** 3 significant digits with an engineering prefix (`820 mV`,
  `19.6 V`, `325 V`). The sign is shown when negative.
- **Percent:** an integer, `<1 %` for anything above zero below 1 %, and above
  100 % when it overflows.
- **Placement:** on the gauge side, next to the bar, upright (reuse the `Label`
  class).
- **When:** the gauge, plate colours and label show only while the simulation
  runs or is paused. The last state stays drawn while paused. When stopped, the
  part looks exactly as it does today. Nothing of the display is saved in
  `.sim1` (only `MaxVolt` and `ChargeScale`, which are part properties).

## 5. Warnings

- **Over-voltage:** `MaxVolt` > 0 and `|V| > MaxVolt`. The gauge fill turns red
  (`QColor(230,40,40)`) and blinks at 2 Hz, and the label (if on) turns red.
  The part is not destroyed. It is a hint, as in led_driver, where C5 (50 V)
  would charge towards 325 V if mains went on without IC1.
- **Reverse polarity** (`elCapacitor`, `V` < −0.1 V; was −1 µV before 2026-11-03, which flagged numerical drift of a discharged part): the existing `m_crashed`
  drawing stays, and the gauge fill is red.

## 6. Code changes

- **`Component`:** `virtual double nominalVolt() { return 0; }`, overridden by
  the sources listed in section 3.
- **`CapacitorBase`:**
  - new property `MaxVolt` ("Rated voltage", V, default 0 = not set, saved);
  - `double capVolt()` and `double chargeLevel()`;
  - `paintCharge( QPainter* )`, which draws the gauge, overflow and tick.
    `Capacitor::paint` and `elCapacitor::paint` call it, and take their plate
    pen colours from `plateColor(int pin)`;
  - `updateStep()` reads `V` and both node voltages, works out what is drawn,
    and calls `update()` only on a visible change. `elCapacitor` keeps its
    crashed check and calls the base.
- **`ChargeView`:** an `Updatable` like `CurrentFlow`. It holds `Vref` and its
  source (`override`, `sources` or `peak`) and the settings, and is reset on
  simulation start and on `Simulator` change. It recomputes the source maximum
  on start and on a property change of any source.
- **Circuit:** the `ChargeScale` property next to `animate` in the `.sim1`
  header.
- **Bounding rect:** `boundingRect()` grows to include the gauge and label
  while the display is on, so they are repainted and selectable correctly.
- **No change to the solver.** With the display off, nothing extra runs.

## 7. Settings, UI and CLI

- **App settings** (in `simulide.ini`, like `currentFlow`; display preferences,
  not saved in circuits):
  - `capCharge` true / false (default false): the gauge and the plate colours.
  - `capLabel` `off | volt | percent | both` (default `off`).
- **GUI:**
  - Its own toolbar toggle button next to the current-flow button: the user
    switches flow and charge separately.
  - Settings > Circuit: a "Show capacitor charge" checkbox and the label combo,
    next to current flow.
  - Circuit properties: "Charge scale (V)", 0 = auto.
- **CLI:**
  - `settings set capCharge true`, `settings set capLabel both`.
  - `circuit set ChargeScale=400` (or the existing circuit-property command).
  - `read C5` gives `voltage`, `level`, `vref`, `vref_source`
    (`override | sources | peak`) and `flag` (`overvoltage`, `reversed`,
    `overflow`). It works paused and with the display off (computed on demand),
    so tests and collaborative sessions can check what the picture shows.
  - `comp set C5 MaxVolt=50`.
- **Docs:** `docs/CLI.md` (settings, the read fields, `ChargeScale`),
  `docs/COLLABORATION.md` (one line: use `read Cx` to confirm a gauge seen in a
  capture).

## 8. Verification

Tests go in `tests/cli/smoke.sh`, headless through the CLI.

1. **RC charge.** 10 V source, 1 kΩ, 100 µF (τ = 100 ms). `vref` = 10,
   `vref_source` = `sources`. At 100 ms the level is 0.632 ± 0.01; at 500 ms it
   is 0.993 ± 0.01.
2. **Divider beside it** (stage R a). Vref = 20 V; C3 at 0.806 V gives a level
   of 0.040.
3. **Switch-off.** The source is switched off: `vref` stays 20 V, and C5's
   level falls with τ = 10 MΩ × 9.9 µF.
4. **Spike immunity.** An inductor with a switch makes a kick far above the
   supply: `vref` doesn't change.
5. **Override and fallback.** `ChargeScale=400`: `vref` 400, `override`. A
   circuit with only a capacitor with InitVolt=5 and a resistor: `vref` 5,
   `peak`.
6. **Rating.** `MaxVolt=10` on a capacitor charged to 15 V: flag
   `overvoltage`; the level is still relative to `vref`.
7. **Polarity and overflow.** A non-polarised capacitor driven negative: level
   positive, voltage negative, the plate on the low side blue. An electrolytic
   reversed: `reversed`. A voltage doubler from 10 V: level > 1, `overflow`.
8. **Plates.** In R a, `capture circuit C5 --scale 6`: top plate red, bottom
   grey, the gauge almost full.
9. **Label formats.** `capLabel` set to each value: the text matches in a
   capture.
10. **Visual check, GUI.** `led_driver/sim/stage_Ra_vdd_dsen.sim1` with
    current flow on: C5's gauge rises as its dots slow, C3's stays near empty,
    and after switch-off C5 stays full while the divider dots stop.
11. **Performance.** The 555 fader and the largest example, display on vs off:
    frame rate and `sim status` speed within 5 %.

## 9. Open questions

1. **Gauge position in tight layouts:** is "opposite the value label" enough,
   or does each part also need its own `GaugeSide` property?
2. **Hidden subcircuit capacitors:** they are not drawn, so they show nothing.
   That seems right; to confirm.

## 10. Later

- **Progress towards the final value** (question (b) in section 3):
  - A DC operating-point solve, with capacitors open and inductors shorted,
    iterating the diodes, redone when a switch or source changes.
  - It gives each capacitor its target `V∞`, so the label can show
    `→ 19.6 V · 63 %` (the textbook charge curve, 63 % at τ), and the gauge
    can mark the target with a tick.
- **Inductors:** the gauge shows |I| / Iref, with the same colour logic.
  Together with the capacitor gauges, this shows energy moving back and forth
  in an LC tank.
- **Batteries:** state of charge, if the battery model gets one.
- **Label formats:** Q = C·V, energy ½CV².
- **Voltage colouring of wires** on the same scale as the plates, a separate
  spec.

## 11. Implementation notes (2026-11-03)

Where the code differs from the text above:

- **The gauge and label are a child item, `ChargeGauge`**
  (`capacitorbase.h/.cpp`), not painted by the capacitor itself.
  - Its transform undoes the parent's rotation and flip, so it draws in
    scene-aligned coordinates.
  - Its bounding rect is its own, so the capacitor's `boundingRect()`, and
    with it the selection highlight, stays unchanged.
  - It takes no mouse buttons, so clicks pass through.
- **The `ChargeScale` setter is `settings set chargeScale V`**, in the circuit
  group next to `capCharge` and `capLabel`. The value is still saved in the
  `.sim1` header (`chargeScale="40"`, only when > 0). In the GUI it is the
  "100 % at" box in Settings > Circuit (Auto = 0).
- **`Vref` is recomputed once per frame**, a pass over the component list
  calling `nominalVolt()`, instead of on source property changes. It is cheap
  and needs no change hooks: the 555 fader runs at the same speed with the
  display on and off.
- **The peak fallback looks only at capacitor pins**, not at every eNode. That
  is enough for its case: a circuit with no source runs on capacitor charge.
- **`elCapacitor::updateStep()` now calls the base `updateStep()`.** As a side
  effect, changing an electrolytic's capacitance during a run takes effect at
  once, as it already did for the plain capacitor.
- **Voltages below 1 µV read `0 V · 0 %`**, not `7.99e-11 µV · <1 %`.

### Tests (`tests/cli/smoke.sh`, section "Capacitor charge display")

Automated:
- RC charge at τ and 5τ (test 1);
- the override, saved and reloaded (test 5);
- the rating (test 6);
- the no-source peak fallback (test 5);
- the reversed electrolytic (test 7);
- the label modes, including rejection of a bad one.

Checked by hand in the GUI:
- led_driver stage R a: C5 at 62 % at τ with its + plate red, C3 at 4 %
  (test 2), and C5 holding while the divider empties after switch-off
  (test 3);
- the performance check (test 11).

Not automated:
- test 4 (spike immunity): it holds by construction, since nominal source
  values can't spike;
- test 7's voltage doubler (overflow): not built yet.

