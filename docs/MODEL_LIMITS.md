# Model limits

What SimulIDE's parts leave out, met while building book circuits with the
create-circuit skill (issues of `jeroenwk/create-circuit`). SimulIDE favours fast,
simple models: none of these is planned as a fix unless a circuit depends on it.
When a circuit does, say so in its description, next to the result it changes, and
build the closest stand-in given here.

## Semiconductors

| Part | Limit | Stand-in / what to do | Issue |
|---|---|---|---|
| BJT | VBE is about 0.6 V at 1 mA (books assume 0.7 V); no IS or VBE setting, `Vcrit` moves the operating point instead | keep the default part, give the difference in the description | [#70](https://github.com/jeroenwk/create-circuit/issues/70) |
| BJT | `Cje` = `Cjc` = 0 pF by default: no Miller capacitance, so no high-frequency roll-off | set `Cje` / `Cjc` from the datasheet when a bandwidth is the point | [#208](https://github.com/jeroenwk/create-circuit/issues/208) |
| BJT, Diode | few named models (no 1N4001, 2N3053, BFY51) | the nearest model of the family (1N4007), a generic BJT with `Gain` | [#239](https://github.com/jeroenwk/create-circuit/issues/239) |
| Mosfet | three pins: no body pin, no body diode | a diode across drain-source when the circuit can forward-bias it | [#212](https://github.com/jeroenwk/create-circuit/issues/212) |
| Zener (Custom) | soft knee: a 1.24 V part holds about 1.17 V at 0.4 mA | read the actual voltage; give the difference | [#118](https://github.com/jeroenwk/create-circuit/issues/118) |
| Led | keeps its full `Threshold` drop at near-zero current (a real LED drops 1.2–1.5 V at 0.1 µA) | an off-state node reads lower than on the bench | [#168](https://github.com/jeroenwk/create-circuit/issues/168) |
| Seven segment | each segment is a 2.4 V threshold + 1 Ω, not a diode | currents differ from a book's LED estimate | [#201](https://github.com/jeroenwk/create-circuit/issues/201) |
| Triac, Diac | the gate trigger is a current into the model: the gate node stays near 0 V, the Diac reads 0 A while it fires | check the firing angle instead of the gate pulse | [#247](https://github.com/jeroenwk/create-circuit/issues/247) |
| all | no temperature: no thermal drift or runaway | show the electrical analogue (vary the bias part) | [#112](https://github.com/jeroenwk/create-circuit/issues/112) |

## Logic

| Part | Limit | Stand-in / what to do | Issue |
|---|---|---|---|
| Gates | `Num_Inputs` at least 2; no separate NOT part | a `Buffer` with `Inverted=true`, or a 2-input gate with its inputs tied | [#253](https://github.com/jeroenwk/create-circuit/issues/253), [#216](https://github.com/jeroenwk/create-circuit/issues/216) |
| Gates | no Schmitt-trigger part (74HC14, CD40106) | a `Buffer` (`Inverted=true` for the inverter) with `Input_High_V` / `Input_Low_V` as the hysteresis thresholds | [#231](https://github.com/jeroenwk/create-circuit/issues/231) |
| Gates | no input clamp diodes; no supply pin | a capacitor kicks an input past the rails (create-circuit lessons.md) | [#265](https://github.com/jeroenwk/create-circuit/issues/265) |
| Encoders | no priority encoder (ENCODER_HEX/BCD are thumbwheel switches) | build it from gates | [#216](https://github.com/jeroenwk/create-circuit/issues/216) |
| Sources | no one-click logic input part | a switch between ground and the logic rail | [#255](https://github.com/jeroenwk/create-circuit/issues/255) |
| CD4066 | a logic model (2.5 V thresholds, logic outputs): it cannot pass analog levels | a MOSFET or relay switch | [#211](https://github.com/jeroenwk/create-circuit/issues/211) |
| Flip-flops | power up in a random state with noise on (a JK sets), whatever S and R hold until they act | a power-on reset, as on the bench | [#194](https://github.com/jeroenwk/create-circuit/issues/194) |

## Passive parts, sources and switches

| Part | Limit | Stand-in / what to do | Issue |
|---|---|---|---|
| Switch, Inductor | ideal: a contact cannot arc; opening an inductor gives a one-step spike, not a plateau | a coil capacitance turns the spike into a ring (create-circuit lessons.md) | [#171](https://github.com/jeroenwk/create-circuit/issues/171), [#172](https://github.com/jeroenwk/create-circuit/issues/172) |
| Battery | 1 mΩ internal resistance: a short draws hundreds of amps and the rail hardly sags | set `Resistance` to the supply's real output resistance | [#191](https://github.com/jeroenwk/create-circuit/issues/191) |
| Transformer | about 0.1 mH of fixed series leakage at a 1:4 ratio (about 1 % droop at full load) | give the droop | [#233](https://github.com/jeroenwk/create-circuit/issues/233) |
| Transformer | with no winding resistance, the DC offset of the first cycle never decays | a small series resistance in the primary | [#235](https://github.com/jeroenwk/create-circuit/issues/235) |
| Thermistor | the dial runs 0–1000 over `Min_Temp`..`Max_Temp`, not in °C | `ui dial` value = °C × 1000 / range; read `Temp` back | [#237](https://github.com/jeroenwk/create-circuit/issues/237) |
| AudioOut | not an electrical load (its `Impedance` is audio only); no bell or buzzer part | a resistor of the coil's value, or a lamp of its rating | [#238](https://github.com/jeroenwk/create-circuit/issues/238), [#240](https://github.com/jeroenwk/create-circuit/issues/240), [#245](https://github.com/jeroenwk/create-circuit/issues/245) |

## Instruments and the canvas

| Part | Limit | Stand-in / what to do | Issue |
|---|---|---|---|
| Meters | no ranges: a digital meter reads any value | give the range the figure's meter has | [#250](https://github.com/jeroenwk/create-circuit/issues/250) |
| Sources | no sweep or curve tracer | step the source with `comp set` and read at each step | [#214](https://github.com/jeroenwk/create-circuit/issues/214), [#251](https://github.com/jeroenwk/create-circuit/issues/251) |
| Oscope | about one sample per µs: a faster edge falls between samples | an instantaneous part reading, `sim average` min/max | [#226](https://github.com/jeroenwk/create-circuit/issues/226) |
| Oscope | the exported history is decimated (samples sit on transitions, far apart) | read the sample instants, or measure short windows (`run --pp --last`) | [#256](https://github.com/jeroenwk/create-circuit/issues/256) |
| Oscope | a rolling buffer (`BufferSize`): a long run overwrites its start | a fresh short run for the start | [#267](https://github.com/jeroenwk/create-circuit/issues/267) |
| Canvas | 3200 × 2400 (x −1600..1600, y −1200..1200) | a long chain in rows | [#205](https://github.com/jeroenwk/create-circuit/issues/205) |
