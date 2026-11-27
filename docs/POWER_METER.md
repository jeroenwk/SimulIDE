# Power meter

A native part (`src/gui/circuitwidget/components/meters/powermeter.{h,cpp}`), Meters in the component list: a meter that goes **in the lead of a source** and shows the power that flows through it, with the r.m.s. voltage, the r.m.s. current and the power factor. It has the ammeter's pins, so it goes in an ammeter's place.

The simulator had no wattmeter: an ammeter shows a current, and the same current at 6 V is half the energy of 12 V. This part shows that at once.

| Property | Default | |
|---|---|---|
| `Reading` | `Instant` | The power now, or `Mean`: the mean over `Avg_Time`. There is no r.m.s. of a power, so `RMS` is not offered (the CLI refuses it; in a file it loads as `Mean`). |
| `Avg_Time` | 100 ms | The block of simulated time over which the mean power, the r.m.s. voltage, the r.m.s. current and the power factor are taken, whatever `Reading` is. |

## What it measures

Put it in the lead of a source whose other terminal is ground: the **red lead (`lPin`) toward the source**, the black lead (`rPin`) toward the circuit.

- **Power** = the red lead's voltage to ground × the current into the red lead. That is the power the red side delivers to the black side, the return being ground. Positive when the source delivers, negative when it takes (a battery on charge). With the source on the black lead, both read negative.
- **V r.m.s.** and **I r.m.s.**: of that voltage and that current, over `Avg_Time`.
- **Apparent power** S = V r.m.s. × I r.m.s.
- **Power factor** = the mean power over `Avg_Time` / S, from −1 to 1. 1.000 for a battery or a resistor; less when voltage and current are out of phase (a capacitor: about 0) or the current is distorted (a rectifier: a half-wave one into a resistor gives 1 / √2 = 0.707). Below 1 µV r.m.s. or 1 pA r.m.s. there is none to give: the face shows `----`, the CLI `null`.

The voltage is taken against ground: in the ground lead the red pin sits a few hundred nanovolt from ground (the meter's own 1 µΩ) and it reads no power.

## Pins and look

The same pins as the ammeter, at the same places: `lPin` (red) and `rPin` (black), 16 px apart, pointing down, and `outnod` at the right. The meter is 1 µΩ, as the ammeter. The face is 84 × 60 px, above the pins, in four lines:

```
 641.3 mW       the power (" avg" when the reading is Mean)
 12.00 V        r.m.s.
 53.40 mA       r.m.s.
PF 1.000        the power factor, or "----"
```

A value is four significant digits with an SI prefix (p, n, u, m, none, k, M, G), as the other meters give it. The face turns and flips with the part, as theirs.

`outnod` gives the power as a voltage, 1 V = 1 W (the reading, so the mean in `Mean`), wired or not.

## Using it

- Drop it on the wire from the source: `comp add PowerMeter --label PM`, `wire add BAT.lPin PM.lPin`, `wire add PM.rPin <the circuit>`.
- An ammeter in a circuit becomes one by changing its `itemtype` in the file from `Amperimeter` to `PowerMeter`: the pins, `Reading` and `Avg_Time` are the same.
- CLI: `read PM` gives `value` (W), `unit`, `reading`, `over` (always: V, I and the power factor are over that block), `v_rms`, `i_rms`, `apparent` and `power_factor`. `sim average --for 100ms PM.lPin` gives the pin's own r.m.s. current to compare with.

Readings on the test circuits (`tests/regress/test_power_meter.py`, [SIMULATED]):

| Circuit | Power | V r.m.s. | I r.m.s. | Power factor |
|---|---|---|---|---|
| 12 V on 330 Ω ∥ 330 Ω | 872.7 mW | 12.00 V | 72.73 mA | 1.000 |
| The same, source on the black lead | −872.7 mW | 12.00 V | 72.73 mA | −1.000 |
| 12 V charging 10 V through 10 Ω: at the 12 V battery / at the 10 V one (red at its +) | +2.40 W / −2.00 W | | 0.200 A | 1.000 / −1.000 |
| 10 V peak, 50 Hz, into 100 Ω | 0.500 W | 7.071 V | 70.71 mA | 1.000 |
| The same into 100 µF | 0.25 mW | 7.071 V | 222.1 mA | 0.0002 |
| The same into 100 Ω + 100 µF | 0.454 W | 7.071 V | 67.38 mA | 0.9529 |
| 100 V peak, 50 Hz, half-wave rectifier into 1 kΩ | 2.476 W | 70.71 V | 49.52 mA | 0.7071 |
| 10 V peak, 50 Hz, rectifier with 100 µF into 1 kΩ | 80.1 mW | 7.071 V | 30.47 mA | 0.372 |

## How it works

A `Meter` (the ammeter's and the voltmeter's base): 1 µΩ in the lead, the current read as the ammeter reads it (`Meter::currentThrough`, which is now the base's so that both use it), the voltage at the red pin.

At every converged step that changes one of its pins (`stepSettled()`, worker thread) the part integrates the voltage and current it has held since the previous step: their squares and their product, each weighted by the time it was held. `Avg_Time` cuts that into blocks, as the other meters' mean and r.m.s. The display and the CLI read the running block (so far), or the last one that closed, without touching the state.

`Reading` decides only the power line: the value now (`instant()`), or the block's mean. The voltage, the current and the power factor are always over the block, also with `Reading` `Instant`: an instantaneous power factor is no quantity. With `Reading` `Instant` on AC the power line follows p(t), as the ammeter's follows i(t): sampled by the display's frames, it flickers. A `Reading` or an `Avg_Time` changed during a run starts a block from that moment.

## Limits

- **Ground is the voltage reference.** The part measures the power between the red side and ground, through this lead: right for the ungrounded lead of a grounded source; meaningless for a source whose other terminal is not ground (a floating secondary, a battery in the middle of a stack), and zero in the ground lead. For those, a Voltimeter across the source and an Amperimeter in its lead.
- **No leading or lagging.** The sign says which way the power flows, not whether the current leads or lags: that needs the reactive power, which the part does not compute.
- **The block, not a cycle.** Over a whole number of cycles of a periodic signal the mean is steady; over another window it moves with the phase (the test: 4 ms blocks on 50 Hz give 0.19 to 0.88 W where a 20 ms block gives 0.500 W).
- The r.m.s. values are those of the solved steps, weighted by their duration: with a staircase source (`WaveGen` at its 100 steps a cycle) the r.m.s. current of a capacitor is the staircase's (0.99 A in the test's circuit where the smooth sine gives 0.22 A): the meter agrees with `sim average` of the pin, and both are right about the waveform they were given. Give the WaveGen `Steps=20000` for a capacitor.

## State (keyframes, going back)

`simState()` saves the held voltage and current, the integrals since the block's start, the block's start and the time integrated to, the last closed block and whether there is one, after the base's own. Going back gives the same state again, with a reading changed during a run too.

## Tests and demo

- `tests/regress/test_power_meter.py` (80 checks): DC in both orientations, a supply below ground, a battery on charge (the source's power is the sum of what the rest takes), the ground lead, AC into a resistor with `Instant` read at four phases and `Mean`, the window (`Avg_Time` 20 ms and 4 ms), a capacitor, an RC, a half-wave rectifier at two amplitudes, a rectifier with a capacitor (its r.m.s. current against `sim average` of the pin), the output pin, save and reopen (also `RMS` in a file), the pins where the ammeter's are, and going back in time.
- `tests/circuits/power_meter_dc.sim1`: the lights of the microscope on 12 V, both LEDs on: 641 mW, power factor 1.000.
- `tests/circuits/power_meter_ac.sim1`: a 10 V peak, 50 Hz sine through a rectifier into 100 µF and 1 kΩ: 80 mW, power factor 0.37.
