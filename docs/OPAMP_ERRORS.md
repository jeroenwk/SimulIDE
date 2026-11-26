# Op-amp errors: slew rate, bias current, offsets, gain-bandwidth

The op amp (`op_amp.cpp`) has five properties in its **Errors** group, all 0 by default, which
leaves the model exactly as before (an ideal op amp with Gain, Out_Imped and rails):

| Property | Unit | What it does |
|---|---|---|
| `SlewRate` | V/µs | The output moves at most this fast. 0 = no limit |
| `Ib` | nA | Input bias current, drawn from each input node into the op amp |
| `Ios` | nA | Input offset current: I(+) = Ib + Ios/2, I(−) = Ib − Ios/2 |
| `Vos` | mV | Input offset voltage, added to V+ − V− |
| `GBW` | Hz | Gain-bandwidth product: one pole at GBW / Gain. 0 = flat gain |

Typical datasheet values: 741 0.5 V/µs, 80 nA, 20 nA, 1 mV, 1 MHz; LF411 15 V/µs, 50 pA,
25 pA, 0.8 mV, 4 MHz; TL071 13 V/µs, 65 pA, 5 pA, 3 mV, 3 MHz; OP07 0.3 V/µs, 1 nA, 0.4 nA,
30 µV, 0.6 MHz. With GBW set, also set `Gain` to the datasheet's open-loop gain (A0): the
pole sits at GBW / Gain, so the default Gain 1000 puts it at GBW / 1000.

## How the slew rate works

A solved step whose output moved more than SlewRate × (time since the previous solved step)
— a step input, a comparator changing rails — switches the op amp to a fourth mode, SLEW:
its output becomes a source that events move by 0.2 % of the supply span per step (from
1 ns to 10 µs apart) towards where the inputs want it. It hands back to the linear (or rail)
model when the target is within one step, or when the direction reverses (with feedback the
open-loop target changes sign as the output crosses the linear solution). After an idle gap
the output is allowed at most one slew step's move: the change started now. A signal from a
sampled source (a Wave Generator's steps) that moves faster than the slew rate slews at each
of its steps, as a real op amp does on a staircase.

## How the gain-bandwidth works

With `GBW` > 0 the linear output follows a single pole at ωp = 2π·GBW / Gain, integrated
implicitly: with a = e^(−ωp·dt) over the time since the last settled step,
out = a·out(previous) + (1 − a)·Gain·(V+ − V− + Vos). That is the linear stamp with the gain
(1 − a)·Gain and a constant a·out(previous), so feedback loops through the op amp are still
solved in one matrix solve and stay stable at any step. An event every `reactStep` moves the
pole on when nothing else changes; lower `reactStep` well below 1/(2π·GBW) for an accurate
step response. A closed-loop stage of gain G rolls off at GBW / G (−3 dB). Rails and the
slew rate apply to the result.

What is left out: further poles (phase margin: a single pole is always stable), output current limits,
input common-mode range, the asymmetric slew rates of some parts (one SlewRate for both
directions), and noise (en, in).

## Checks

`tests/regress/test_opamp_errors.py` (16 checks against hand calculations): Vos on a follower
and a ×101 stage; Ib on 1 M to ground and in a 1 M / 1 M inverting stage; Ios; integrator
drift from Ib (+10 V/s with 100 nA into 10 nF, AoE Fig. 4.65) and from Ib + Vos; a 0 → 10 V
step on a 0.5 V/µs follower (2 V after 4 µs, 10 V after 20 µs, and down again); a 13 V/µs
comparator swinging 30 V in 2.3 µs; a follower with no slew rate following 100 kHz within
0.1 mV; the 0.5 V/µs follower turning a 100 kHz, 10 V sine into a 2.5 V p-p triangle
(AoE Fig. 4.37's distortion). The earlier op-amp regressions (BH-032, CC-368) are unchanged.
Keyframes (going back in time) stay identical with all four set.

`tests/regress/test_opamp_gbw.py`: a GBW 1 MHz follower reaches 0.633 V 159 ns after a 1 V
step (1 − e⁻¹ = 0.632 with τ = 1/(2π·GBW)) and 0.998 V after 1 µs; with GBW 0 the step is
immediate; an inverting ×10 stage gives 0.999 V peak for 0.1 V at 1 kHz and 0.706 V at
GBW/11 = 90.9 kHz (−3 dB: 0.707). Scherz Fig. 10.22 A (32.768 kHz crystal, op amp at Gain
1000) runs at the same frequency with GBW 0, 1 MHz and 100 kHz.
