# Part states for active components: implementation note

Status (2026-11-04): steps 1 and 2 done. These are the shared mechanism and the thyristors (SCR, triac, diac); the BJT and MOSFET are moved onto it. Steps 3 to 5 are open.

## What a part state is

With **Show Part States** on (toolbar, or `settings set partStatus true`), a part that reports its state shows it two ways:
- a badge above the part;
- its outline in the state's colour.

Messages logs every change with its simulated time when Log part states is on. `simulide cli status <comp>` lists them, and `read <comp>` gives the current one as `state`.

Scripted datasheet parts report through `component.setStatus()` in their AngelScript cores. Built-in parts report their **operating state**, worked out from their own voltages and currents.

Colours follow one rule across parts:

| Level | Colour | Used for |
|---|---|---|
| off | grey | not conducting: cut-off, pinched off, blocking |
| on | green | the normal working state of an amplifying or regulating part: linear, active, regulating |
| wait | amber, filled | fully on, like a closed switch: saturated BJT, ohmic FET, conducting thyristor; and a part at a limit (op amp at a rail, regulator in dropout) |
| fault | red, filled | outside its ratings: diode in reverse breakdown, LED over its maximum current |

## How it works (step 1, done)

The pieces, in the order a state passes through them:

- **`StateLog`** (`src/gui/circuitwidget/statelog.{h,cpp}`) holds one source of states:
  - the current state, plus every change with its simulated time (up to 1000);
  - a global sequence number, so changes from different parts sort in order;
  - a mutex, since states are written in the simulation thread and read in the GUI thread.

  `shown()` gives the state to draw this frame. A more severe state that came and went within one frame is held on screen for 3 frames. Scripted cores (`ScriptCpu`) use the same event type and sequence counter, so EventLog, StatusView and the CLI treat both kinds alike.

- **`Component`** gives a part the log. A part that reports states:
  1. calls `enableStates( show )` in its constructor;
  2. lists `showStateProp()` among its properties. This is the **Show State** property; with it off, the part has no badge, no Messages lines and no `status`;
  3. calls `clearStates()` in `initialize()`;
  4. calls `reportState( text, level )` from the simulation thread. Only changes are recorded.

- **Settled steps** (`Simulator::addToSettled`, `eElement::stepSettled`). Inside a time step a non-linear part's `voltChanged()` runs once per iteration, and the values in between are not final. Taking a state there would log states the step never had. Taking it once per frame instead, as the first version did, misses anything faster than a frame, so states only made sense with the simulation slowed down. Now:
  - a part calls `Simulator::self()->addToSettled( this )` from `voltChanged()`; this is cheap, and only the first call in a step counts;
  - once the step has converged, the simulator calls `stepSettled()` on each part that asked, with that step's settled voltages;
  - only parts whose voltages moved pay anything;
  - a generation counter forgets the requests on stop and reset without touching elements that may already be deleted.

  The times are therefore exact, at full speed. Messages marks these lines as exact (`approx` false in `log`).

- **Hysteresis**: each part keeps its last state and only changes it once a boundary is passed by a margin, so the state does not flicker on a boundary.

Example (Duncan Fig. 49.5, 100 mV in, full speed) [SIMULATED]:

| Time | State |
|---|---|
| 40 µs | SAT |
| 320 µs | ACTIVE |
| 730 µs | OFF |
| 920 µs | ACTIVE |
| … | every 1 ms |

## Parts

| Part | States | Basis | Status |
|---|---|---|---|
| BJT | OFF / ACTIVE / SAT | OFF: VBE < 0.45 V (on above 0.5 V). SAT: VCE < 0.2 V (back to active above 0.3 V). PNP reversed | done |
| MOSFET (incl. depletion, used for JFETs) | OFF / ACTIVE / OHMIC | Vov = VGS − Vth of the model. OFF: Vov < 0. OHMIC: VDS < Vov (0.05 V hysteresis). P channel reversed | done |
| SCR | OFF / ON | triggered (`m_state`) **and** anode current > 10 % of the holding current. With gate current present, `m_state` stays set while the series diode blocks the reverse half; that is not conduction | done (step 2) |
| Triac | OFF / ON | same, with \|current\|: OFF at each zero crossing | done (step 2) |
| Diac | OFF / ON | `m_state`: on above the breakover voltage, off below the holding current | done (step 2) |
| Op amp | LINEAR / SAT+ / SAT− | output vs `m_voltPos` / `m_voltNeg`, which `voltChanged` clamps to | step 3 |
| Voltage regulator | REGULATING / DROPOUT / OFF | its own test: input − output < 0.7 V = dropout, input < 0.7 V = off | step 3 |
| Diode | OFF / ON / BREAKDOWN (red) | forward voltage vs threshold, reverse vs breakdown | step 4, Show State off by default |
| Zener | OFF / FORWARD / REGULATING (green) | as the diode; for a zener, breakdown is the working state | step 4 |
| LED | OFF / ON / OVER (red) | its current against its maximum | step 4, Show State off by default |
| 555 | output HIGH / LOW, DISCHARGE | its flip-flop | step 5, if useful |
| Relay, analog mux | ENERGISED / channel n / DISABLED | coil state, address | step 5, if useful |

Checked on 2026-11-04 [SIMULATED], 50 Hz AC through a resistor, gate driven from 5 V through 100 Ω:
- **SCR:** ON 0.2–10 ms and 20.2–30 ms, OFF in the negative half-cycles.
- **Triac:** ON in both halves, OFF at each zero crossing.
- **Diac** on 40 V peak through 1 kΩ: ON at 30 V (2.8 ms), OFF when its current falls below 10 mA (8.8 ms).

## Things to keep in mind

- **Clutter.** A circuit with many diodes would get a badge on every one. That is why Show State is per part, with the default chosen per type: on for transistors, thyristors, op amps and regulators; off for diodes and LEDs.
- **A state is only as good as the solved values.** Before adding a state, check how the model decides it has converged:
  - **MOSFET**: a fixed 5 µA. This was 1 % of the 0.43 mA in Duncan's FET amplifier and put steps into the output. It is now relative, 1e-4 of the current, never looser than 5 µA.
  - **Voltage regulator**: stops at a fixed **1 mA** change, worse still. Fix it the same way in step 3.
  - **Op amp**: uses 5e-6 V on its input and output. Fine for the states.
- **Thyristor models report "triggered", not "conducting".** See the SCR row. The badge shows conduction.
- **Subcircuits**: only top-level parts get badges. A transistor inside a subcircuit (a datasheet part's internals) shows nothing; the part's scripted cores report for it.
- **Saved circuits**: Show State is saved with each part (`Show_State`). Circuits from before this change load with the type's default.
