# Op-amp errors: slew rate, bias current, offsets

The op amp (`op_amp.cpp`) has four properties in its **Errors** group, all 0 by default, which
leaves the model exactly as before (an ideal op amp with Gain, Out_Imped and rails):

| Property | Unit | What it does |
|---|---|---|
| `SlewRate` | V/µs | The output moves at most this fast. 0 = no limit |
| `Ib` | nA | Input bias current, drawn from each input node into the op amp |
| `Ios` | nA | Input offset current: I(+) = Ib + Ios/2, I(−) = Ib − Ios/2 |
| `Vos` | mV | Input offset voltage, added to V+ − V− |

Typical datasheet values: 741 0.5 V/µs, 80 nA, 20 nA, 1 mV; LF411 15 V/µs, 50 pA, 25 pA,
0.8 mV; TL071 13 V/µs, 65 pA, 5 pA, 3 mV; OP07 0.3 V/µs, 1 nA, 0.4 nA, 30 µV.

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

What is left out: the gain-bandwidth product (the gain is flat), output current limits,
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
