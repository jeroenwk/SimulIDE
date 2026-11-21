# Simulation control: keys, skip, speed, history and snapshots

Status: **phase 1 merged** (main b0096de, 2026-11-21), §3.1–3.8; **speed lock merged**
(main 7a697b7, §3.9); phase 2 started on `feature/sim-history` (§4.7: determinism measured); differences from this text are noted in place. All decisions
taken (D1–D15, Decision log §9). Phases 2 and 3 are studies.

Goal: control a running simulation the way you control a media player or a scope: play/pause,
stop and skip forward from the keyboard, set the skip interval and the speed by typing, and
later step **back** in time, keep snapshots of a state and set triggers that take them.

## 1. Requirements (user, 2026-11-21)

1. When the simulation starts, the canvas loses keyboard focus.
2. **Space**, or the macOS play/pause key (above F8), pauses and resumes; it starts the
   simulation if it is stopped. (Media keys dropped, D2: F5 start/stop, F6 pause/resume.)
3. **Esc** stops the simulation.
4. **↑ / ↓** set a *skip interval*, from one solver step, through picoseconds and microseconds,
   up to 1 minute. Shift and Option change the factor (×5, ×10, ×100).
5. The skip interval can also be edited in the toolbar.
6. **→ / ←**, and the macOS skip keys (above F7 / F9), pause and skip by the skip interval.
   (Media keys dropped, D2.)
7. The speed in the toolbar can be edited (typed), not only picked from the list.
8. Going back in time: what it means for the implementation, whether a full snapshot per
   solver step fits in memory, and the memory and CPU cost (may be phase 2).
9. Snapshots saved with or next to a circuit, to inspect a state later; triggers that make a
   snapshot, as a scope's trigger does.

## 2. The code as it is (2026-11-21)

- **Run controls** (`CircuitWidget`, `src/gui/circuitwidget/circuitwidget.cpp`): `powerCircAct`
  (F5, start/stop toggle), `pauseSimAct` (F6), window shortcuts (`QAction`). F7–F10 and F12 are
  view toggles (current flow, capacitor charge, part states, instruments, noise).
- **Speed box**: `SpeedBox`, a non-editable `QComboBox` with 11 presets from 1 µs/s to 10 s/s;
  it turns orange and shows the real speed when the simulation can't keep up. Picking one calls
  `Cli::setSimSpeed()` (`src/cli/cliapi.cpp`), which keeps steps/s below 1e9 by changing the
  step size (with a note), and marks the circuit changed (the speed is saved with it).
- **Canvas keys** (`KeyboardController`, specs/keyboard-input.md): with the canvas focused, the
  arrows move the keyboard cursor (the cross) or the selection, Space edits the style of the
  selected wire or does `spaceKey()`, Esc cancels a wire being drawn or leaves network mode.
  `CircuitView::enterEvent` gives the canvas focus as soon as the mouse enters it.
- **Switch keys**: a switch's `Key` property is Shift + A–Z while the simulation runs. No
  conflict with the keys below.
- **Simulator** (`src/simulator/simulator.cpp`): event-driven. Time is `uint64_t` picoseconds.
  Each 50 ms timer tick (`timerEvent`) runs the worker (`runCircuit`) up to
  `circTime + psPerFrame`: it pops all events with the same timestamp, runs them, solves the
  matrix (`solveCircuit`, Newton iterations for non-linear parts) and moves to the next
  timestamp. `m_stopAt` holds time at a point (CLI `sim run --for`); `holdWorker()` makes the
  worker finish the step it is solving, so the GUI and CLI read a converged step.
- **"One solver step"** is therefore *the next event timestamp*, solved. It is not a fixed
  time: with only reactive parts it is `reactStep` / `stepSize`, with a 555 or an MCU it can be
  nanoseconds, in a static circuit there may be no next event at all.
- **State** lives in ~120–140 element classes (`runEvent` / `voltChanged` implementations),
  the node voltages (`eNode`), the admittance matrix and current vector (`CircMatrix`, dense per
  group), the event queue (a linked list through the elements), the noise engine, and per MCU
  its data space (`McuDataSpace::m_dataMem`: RAM + registers), EEPROM, CPU and peripheral state.
  Script parts (`components/`, AngelScript cores) keep state in script variables. Nothing can be
  saved or restored today.

## 3. Phase 1: keys and toolbar

### 3.1 Focus and key routing

The canvas keeps its keys (D1): **when the canvas has focus, Space, Esc and the arrows work as
today** (keyboard editor) and don't control the simulation. The simulation keys work when the
canvas does not have focus, i.e. after a start, until the canvas is given focus again.

- While the simulation is running or paused, a **window-level key filter** (an event filter on
  `MainWindow`, installed at start and removed at stop) handles Space, Esc and the arrows when
  the focused widget is neither the canvas nor a widget that uses these keys itself: a text
  field (a property field, the code editor, a search box), a list or tree while stopped (the
  component list, the file browser: running or paused, they leave these keys to the simulation,
  user 2026-11-21), an editable combo such as the speed or skip box
  (`CircuitWidget::keysToSim`).
- At start, the canvas is cleared of focus (`clearFocus()`) and the keyboard cursor is hidden.
  A canvas mode in progress (wire drawing, network walk, value editing) is **not** ended yet
  (left out of phase 1): it waits until the canvas gets the focus back.
- The canvas never takes focus when the mouse merely enters it, in any state (user,
  2026-11-21; before, it did while stopped): only a **click** on the canvas gives it focus
  (keyboard editor keys again).
- A **click anywhere else** in the window (the toolbar and its buttons, Messages, a panel)
  takes the focus from the canvas (`CircuitWidget::eventFilter`, on the mouse press); a widget
  that takes focus itself (a text field) gets it. A read-only text such as Messages does not
  keep the simulation keys from working (`keysToSim`); Messages is read-only (select and copy
  only), it was an editable text and kept the keys. A click on a part's line in Messages selects
  the part and moves the view only when the part is out of sight (before: always centred).
- The canvas accepts **no touch events** (`WA_AcceptTouchEvents` off): on a Mac trackpad a finger
  resting over the canvas is a touch, which Qt treats as a click for the focus, so the canvas took
  the keys while the pointer only passed over it. Pinch zoom and scrolling are gesture and wheel
  events and still work.
- **Esc hands the keys back** (D11): on the canvas while running or paused, Esc first does what
  it does today (`KeyboardController::escKey`, `Circuit::keyPressEvent`): ends a link or a wire
  being drawn, leaves network mode or a value edit, clears the selection. When none of these
  applies (nothing in progress, nothing selected), it clears the canvas focus and the
  simulation keys are active again; the skip box gets its blue frame again. So from a
  selected part it is Esc, Esc. That Esc never stops the simulation: only an Esc while the
  canvas has no focus does.
- The toolbar shows which one has the keys: the skip box has a blue frame while the simulation
  keys are active (`updtKeyOwner`, on every focus change).
- F5 (start/stop) and F6 (pause/resume) are window shortcuts and work whatever has focus (D2).
- At stop, the filter is removed; the canvas gets focus again only with a click.

### 3.2 Keys

Simulation keys (canvas not focused, §3.1); F5 and F6 work everywhere:

| Key | Stopped | Running | Paused | Skipping |
|---|---|---|---|---|
| F5 | start | stop | stop | stop |
| F6 | — | pause | resume | cancel the skip, stay paused |
| Space | start | pause | resume | cancel the skip, stay paused (D3) |
| Esc | — | stop | stop | cancel the skip, stay paused; a second Esc stops (D3) |
| ↑ / ↓ | change the skip interval (§3.3), in every state | | | |
| → | start, skip one interval from t = 0, pause there (D4) | pause, skip forward | skip forward | ignored (no queue) |
| ← | — | pause, skip back (phase 2) | skip back (phase 2) | ignored |

- Shift, Option and Shift+Option on → / ← skip 5×, 10× and 100× the interval (D5).
- In phase 1, ← writes "Going back in time needs the history" in Messages.
- **Auto-repeat**: holding → skips again after each skip has finished and its frame has been
  drawn; repeats that arrive during a skip are dropped, so releasing the key stops at once.
- F5 and F6 keep their current actions (`powerCircAct`, `pauseSimAct`); F7–F10 and F12 keep
  their view toggles. **No macOS media keys** (D2): they reach an app only when it makes itself
  the system's Now Playing app (`MPRemoteCommandCenter`), which takes them from the music
  player.

### 3.3 Skip interval

- Values: **`1 step`** (the next event timestamp, solved), then a 1–2–5 ladder from **1 ps** to
  **1 min** (1 ps, 2 ps, 5 ps, 10 ps … 500 ms, 1 s, 2 s, 5 s, 10 s, 20 s, 30 s, 1 min), 43 values
  plus `1 step`.
- ↑ makes it larger, ↓ smaller. Factors (D5):

  | Modifier | Factor | e.g. from 1 µs |
  |---|---|---|
  | none | one ladder notch (×2 / ×2.5) | 2 µs |
  | Shift | ×5 | 5 µs |
  | Option | ×10 | 10 µs |
  | Shift + Option | ×100 | 100 µs |

  A product is rounded to the nearest ladder value and clamped to 1 ps … 1 min; ↓ from 1 ps
  goes to `1 step`. The skip box shows each change.
- An interval shorter than the step size is allowed and shown with a note ("1 ps < step 1 µs:
  skips to the next step"); the skip always ends on a solved step.
- Saved with the circuit, as the speed is (`skipInterval` in the simulation settings) (D6).
- **Follows the speed** (user, 2026-11-21): the interval is a tenth of the first speed in the speed
  list at or below the speed reached: 1 µs/s → 100 ns, 1 ms/s → 100 µs, 25 ms/s (10 ms/s) → 1 ms,
  1 s/s → 100 ms. Set when the speed is set (toolbar, `sim speed`, `settings set speed`), and
  while running (not paused, not skipping) from the **real speed** when it is below the set speed
  (the orange speed, speed lock off): board_mains set to 1 s/s but reaching 25 ms/s gets 1 ms. A
  10 % margin before moving up a list step keeps it from flipping. It changes only when that list
  step changes, so a value set by hand (↑ / ↓, the box) stays until then. A run starts from the
  interval as it is (the one saved with the circuit). `1 step` is kept.

### 3.4 Skip forward

A skip pauses the simulation (if running), runs from the current time to *t + interval* and
pauses there. It reuses the `sim run --for` mechanism (`m_stopAt`), then waits for one frame so
the meters, scope and part views show the new state.

- **Speed during a skip** (D7): as fast as the computer can, no real-time throttle; the point
  of a skip is to get there.
- A long skip (1 min of a 1 µs-step circuit is 6·10⁷ steps) shows its progress in the toolbar
  time and can be cancelled (§3.2). One that takes over 300 ms of wall time, is cancelled or
  fails gets a line in Messages ("Skipping 1 min from 2 ms …", "Skip: +1 min → 1 min 2 ms");
  a quick one only moves the time.
- `1 step`: a new `Simulator::stepOnce()` runs the events of the next timestamp, solves and
  pauses. If there is no next event, it says so ("no event pending: the circuit is static").

### 3.5 Toolbar

Layout (user, 2026-11-21, built):

- **Stopped**: one row, the editing bar, then only the play button; the Info menu at the right
  end. Speed, skip, the other run buttons and the time are hidden.
- **Running, paused or debugging**: always two rows, even when one would fit. The first is the
  editing bar with the Info menu at its right end; the second is the simulation bar:
  `[speed ▾] [skip ▾] ⏮ ⏹ ⏯ ⏭  time  [NOISE]`. A stop (not a pause) collapses it again
  (`CircuitWidget::setSimExpanded`).
- **No status block**: the time's colour gives the state, green running, light grey paused.
  Other status messages (`CircuitWidget::setMsg`: simulator warnings and errors) go to
  Messages, once per run; answers to a key or a typed value (a refused skip, a speed that
  doesn't parse, ← in phase 1) every time.
- **No info pane** (speed, loads, FPS, main MCU): its numbers are one line in Messages when the
  simulation pauses or stops while running, e.g. `Paused at 1 s 500 ms 000 µs · set 1 s/s ·
  real 886 ms/s (88.6 %) · sim load 14 % · GUI load 0 % · 20 FPS` (`logRunInfo`; the real
  speed is the last full second's, so it reads low just after a start). The `InfoWidget`
  stays, hidden: it still computes the toolbar time and the orange real-speed box.

- **Speed box**: editable `QComboBox` (presets stay in the list). Accepted: a time per second
  (`3 ms/s`, `3ms`, `0.25 s/s`) or a percentage of real time (`25%`, `0.3 %`), parsed with
  `Simulator::parseSi`. Enter applies through `Cli::setSimSpeed()` (its notes go to Messages);
  Esc in the field restores the value; a value that doesn't parse turns the field red, keeps
  the old speed and says so in Messages. The orange real-speed override stays, shown only while
  the field is not being edited. Range 1 ps/s … 1000 s/s.
- **Skip box**: the same kind of editable combo, the ladder as its list, `1 step` first.
  Accepted: `1 step`, `step`, any time `1ps` … `1min` (also `60s`); a typed value off the
  ladder is kept as typed (↑/↓ then move to the next ladder value).
- Tooltips give the keys (`KeyText::tipWithKey`): "Skip interval (↑ ↓, ⇧ ×5, ⌥ ×10, ⇧⌥ ×100)",
  "Skip forward (→)".
- Two buttons ⏮ ⏭ around stop and pause (D8). ⏮ is disabled
  until the history exists (phase 2) and for a circuit without history (§4.6).

### 3.6 CLI

Everything the keys do has a CLI command (CLI.md, Simulation):

| Command | |
|---|---|
| `sim skip [<time>\|step]` | Skip by the given interval, or by the skip interval; replies with the old and new time |
| `sim skip --back [<time>\|step]` | Phase 2 |
| `sim step` | One solver step (= `sim skip step`) |
| `settings set skipInterval <time>\|step` | The toolbar skip box |
| `ui type <keys> --window` | Real key presses to the focused widget (the window when none), the canvas focus left as it is: tests the key routing |
| `sim status` | gets `skipping: true` while a skip is not done (it pauses at its end, then waits one frame) |

A run starts at t = 1 ps (`Simulator::resetSim`), so the reactive parts' events fall on
1 ps + k·step. A skip is measured from the real start time, as `sim run --for` is: `sim skip
1ms` from stopped lands on 1 ms + 1 ps. Measured from 0 it stopped 1 ps before an event, one
reactive step behind `sim run --for` (found by the test).

### 3.7 Tests (phase 1)

`tests/regress/test_simctl_skip.py` (28 checks): exact landing of `sim skip` from stopped, by the
interval and with `--count`; `sim step` on the next event; a skip and `sim run --for` give the
same capacitor voltage bit for bit; 10 ms at a set speed of 1 µs/s skipped in 0.1 s of wall
time; `skipInterval` saved and reopened; every key (Space ×3, ↑ ↓ with each modifier, → ⇧→,
Esc, → from stopped), Space ignored while the canvas has the focus, Esc on an idle canvas handing
the keys back. Still by hand: typing in the speed and skip boxes, the blue frame, the Settings
dialog note.

### 3.8 Simulation settings changed during a run

The Settings dialog (`AppDialog`, Simulation tab) is a separate, non-modal window: it can be
open and edited while the simulation runs. Its keys never reach the simulation key filter (that
filter is on the main window). What each setting does when changed during a run, from the code
(2026-11-21):

| Setting | When it takes effect | Danger |
|---|---|---|
| Speed (% slider, steps per second, step unit) | at once (`setStepSize`, `setPsPerSec`, as the toolbar box) | none. Note: **step size is not a solver step**: no element reads `stepSize()` during a run; it only sets how speed is counted (steps per second). The time resolution comes from the parts' own events and the reactive step |
| FPS | at once (frame length) | none; frames are where keyframes are taken (§4.3), so fewer FPS = keyframes farther apart = a slower step back |
| Reactive step | the global value at once, but each capacitor, inductor and transformer reads it only at start (`eReactive::updtReactStep`, `Transformer` keeps its own copy) or when its fine step changes | confusing, not harmful: a change during a run mostly applies at the next start |
| Max. iterations (NonLinear) | at once (`setMaxNlSteps`) | changes results only on a step that does not converge |
| Slope steps, supply ramp, noise group | at the next start (read in `initialize` / `startNoise`) | none during the run |

**Stopping the simulation is not needed** (D13, decided): nothing in the dialog changes the circuit
structure or the matrix pattern, which is what would need a stop. Three changes are proposed
instead:

1. The dialog marks the settings that apply **at the next start** ("applies at the next
   start") while the simulation runs, and the Messages line says so after such a change, so
   nobody waits for a change that isn't coming.
2. A change during a run is written to Messages with its simulated time ("t = 12.5 ms:
   max. iterations 100000 → 1000"), as results after it may differ.
3. **History (phase 2)**: a setting that changes results (max. iterations, the reactive step
   when it applies, the speed lock's step factor of §3.9) is an entry of the input journal, with
   its simulated time, so a replay gives the same result; every keyframe holds the settings in
   force. Speed and FPS don't change results and are not journalled.

### 3.9 Speed lock: hold the set speed by coarsening the step (requirement, 2026-11-21)

Today, when the computer can't reach the set speed, the simulation runs as fast as it can and
the speed box shows the real speed in orange. The user asked for a toggle that **holds the set
speed** instead, by making the simulation coarser while the CPU can't keep up.

- **What can be coarsened**: only the reactive step (capacitors, inductors, transformers) — the
  step size setting has no effect on the solver (§3.8). The parts already have a live path to
  change their step during a run, with the stored current rescaled (`eReactive::setFineStep`:
  new admittance, `rescaleSource`, the next event rescheduled); the lock uses the same path with
  one global factor (`baseStep() × m_stepScale`).
- **What can't be**: event-driven parts set their own pace: clocks, wave generators, a 555's
  edges, logic, MCUs. A circuit dominated by them can't be sped up by the lock; the speed box
  then stays orange and the lock says "can't keep up: events, not steps".
- **Control loop**, once per frame: if the real speed stays below 95 % of the set speed for 3
  frames, double the factor; if the load is below 50 % for 10 frames, halve it, never below ×1.
  Upper limit: the **max. reactive step** setting (`speedLockMaxStep`, saved with the circuit,
  default 100 × the reactive step) (D14); at the limit the speed box turns orange again.
- **Shown**: an orange badge beside the speed box, as noise has ("step ×8"), with the actual
  reactive step in its tooltip; `sim status` gives the factor; the factor changes go to
  Messages with their simulated time. Accuracy drops with a coarse step (an RC with τ near the
  step is wrong), so results under a factor > 1 are flagged in CLI replies (`note`).
- **Skips and `sim run --for`** run as fast as possible (D7) and have no speed to hold: they use
  the factor ×1 (D15).
- **Determinism**: the factor follows the CPU load, so two runs differ. For the history, each
  factor change is a journal entry (§3.8), so a replay repeats it exactly.
- Saved with the circuit (`speedLock`, bool, off by default); a toggle in the Settings dialog
  and a checkable entry in the speed box's list; CLI `settings set speedLock true`.

**As built (2026-11-21)**:
- `eReactive::setStepScale` (capacitors, inductors, junction capacitances) shares the live path
  of `setFineStep` (`applyStep`); the noise step still wins when finer. **Transformers keep
  their own step** (they are not `eReactive`), as with noise.
- The measure is the share of its frame span the worker managed (`simPsPF / psPF`), not the
  wall-clock speed: a slow GUI is not the solver's fault and a coarser step would not help.
  The factor changes in the GUI timer after `holdWorker()`, with the worker idle.
- A skip (`setFastRun`), `sim run --for` and `sim average` (`setStopAt`) put the factor at ×1
  at once, while they hold the worker: a skip shorter than a frame would otherwise run coarse.
- The toggle is a lock button after the speed box (not an entry in its list: a combo box has
  no checkable rows); the orange badge "step ×N" (" max" at the limit) after it. Settings:
  a "Speed lock" group with the max. reactive step (empty = 100 × the reactive step).
  CLI: `settings speedLock`, `settings speedLockMaxStep`; `sim status` gives `speedLock`
  {on, scale, step, max_step, limited} and a `note` once the factor went above ×1 in the run.
- Messages: a new largest factor of the run, the return to ×1, and "can't keep up" (at the
  max. step, or "events, not steps" with no capacitor or inductor); the other changes go to
  the debug log (`[lock]`), as the factor can swing up and down every second.
- Measured [SIMULATED, test_simctl_speedlock.py]: 50 Hz sine into 1 kΩ · 1 µF with a 1 ns
  reactive step runs at 3 % of real time unlocked; locked it settles at ×32 (32 ns) and 96 %.

## 4. Phase 2: going back in time

### 4.1 What "back" means

To show the circuit at an earlier time *t*, every piece of simulation state must be what it was
at *t*: node voltages, every element's internal state (capacitor voltage and current history,
inductor current, diode and transistor operating points, logic levels, timers, counters,
relays), the event queue with each event's time, the noise generator's state, each MCU's RAM,
registers, peripherals and CPU state, and script variables of script parts. The displays (scope
traces, plotter, event log, meters) are cut back to *t*. Resuming from *t* then gives exactly
the same future, unless the user changes something.

There are three ways to get there; only the third is practical.

### 4.2 Sizing (estimates, [INFERRED])

A typical analog circuit of 50 nodes and 100 parts; a mixed one adds an ATmega328.

| Piece | Analog | + ATmega328 |
|---|---|---|
| Node voltages (8 B each) | 0.4 KB | |
| Element state (~16–64 B each) | 2–6 KB | |
| Event queue (time + order per element) | 1–2 KB | |
| MCU RAM + registers + I/O, EEPROM, CPU and peripherals | | +4 KB (flash only if the program writes it) |
| Matrix | not stored: restamped from the elements after a restore | |
| **One full snapshot** | **~5 KB** | **~10 KB** |

Steps per second at 1 s/s (real time) with a 1 µs step: 10⁶ (`stepsPerSec`); an MCU at 16 MHz
adds its own event rate.

| Approach | Data rate | 1 GB lasts | CPU |
|---|---|---|---|
| A. Full snapshot every solver step | 5–10 GB/s | 0.1–0.2 s | copying 100 scattered objects per step: 2–5× slower simulation |
| B. Delta per step (only what changed: a few node voltages and elements per event) | 50–200 MB/s | 5–20 s | +20–50 % per step, and every element must report its changes |
| C. **Keyframes + deterministic replay**: a full snapshot every frame (50 ms wall, 20/s), and the user's inputs with their time | 100–200 KB/s | 1.5–3 h | one snapshot per frame: < 1 % |

Answer to requirement 8: **a full snapshot per solver step does not fit** (gigabytes per
second) and would slow the simulation several times. Keyframes plus replay keep the cost below
1 % and still give *any* earlier step. **Decided: a snapshot every frame (approach C).**

### 4.3 Keyframes + replay (approach C)

- **Recording**: at each frame, at a converged step boundary (the worker is already held there
  by `holdWorker()`), the simulator writes a keyframe: simulated time, all element states, node
  voltages, event queue, noise state, MCU states. Keyframes go into a ring buffer with a memory
  budget (setting, e.g. 256 MB); older ones are thinned (all of the last minute, then 1 in 10,
  then 1 in 100), so a long run keeps a coarse history from its start.
- **Input journal**: every change the user makes while it runs — a switch, a push button, a
  potentiometer, a keypad key, a property changed live, a serial console byte, a simulation
  setting that changes results (§3.8), a speed-lock factor (§3.9) — is stored with its simulated
  time and replayed at that time.
- **Going back to t**: restore the last keyframe at or before *t*, restamp the matrix, then run
  forward to *t* as fast as possible (with the inputs from the journal), and pause.
  Between keyframes lies at most one frame of simulated time, which took at most ~50 ms of wall
  time to compute when the simulation kept up, so **a step back costs ≤ ~50 ms**: ← held down
  is as interactive as →. A step back of `1 step` replays to the event timestamp before the
  current one.
- **Resuming from an earlier time** discards the recorded future (keyframes and journal after
  *t*), as an editor's undo discards redo after a new edit; no branches (D9).
- **Editing the circuit** (adding, deleting, rewiring a part) ends the history: the stored state
  no longer matches the circuit.
- A timeline (a slider under the toolbar, from the oldest keyframe to now) can follow once
  this works.

### 4.4 Determinism: what replay needs

Replay gives the same result only when the simulation is bit-for-bit deterministic. Today it
is, for the same binary and the same inputs, except for:

- **Noise**: the seeded generator's state must be in the keyframe (per element: `noiseSeed`).
- **Real outside world**: a serial port to real hardware, a real clock, a joystick: replay
  can't reproduce them; recording their input in the journal covers incoming data; parts that
  can't be journalled turn history off with a message naming them.
- **Unsolved steps**: `breakStep()` can leave a step unsolved after 50 ms; keyframes are taken
  only at converged boundaries.
- **Script parts**: AngelScript variables need a save/load hook in each script, or the part
  declares it has no history.

### 4.5 Element state API

Each element class gets two virtual functions: `saveState( StateWriter& )` and
`loadState( StateReader& )`. The default reports "no history support"; when a circuit
contains such a part, history is off and Messages names the parts (as noise does with
`noiseOff`). Rollout class by class, most used first: sources, R, C, L, diode, BJT, MOSFET,
switches, logic gates and flip-flops, 555, op amp, meters, scope, then MCUs, then script parts.

The test for each class is a **round-trip check**: run to t₁, save, run to t₂ recording every
node voltage per step, restore t₁, replay to t₂, and require identical values bit for bit.
A CLI command (`sim history check`) runs it on any circuit; the regression suite runs it on
every sample and book circuit, which also shows which parts are still missing the API.

Effort [INFERRED]: the simulator side (keyframes, journal, replay, ring buffer) is a few days;
the per-class work is the bulk, ~120–140 classes, most with a few fields. With the scope of
§4.6 the MCU cores and peripherals, the largest part, are left out.

### 4.6 Scope: circuits, not MCUs (decided 2026-11-21)

Going back and snapshots may be limited to circuits (D12): electrical and logic parts. A
circuit with a microcontroller, a script part (AngelScript core, `components/`) or a link to
the outside world (a serial port, a real clock) has **no history**: ⏮ and ← are disabled, the
Messages names the parts that prevent it, and skip forward works as in phase 1. MCUs and
script parts can get the state API later, part by part, without changing this design.

### 4.7 Measured: determinism and replay speed (2026-11-21)

**Determinism** [SIMULATED]: 11 circuits were each run to the same time by four routes (frames
at 100 % speed, a skip, frames at 10 %, a skip again), and every node voltage was compared bit
for bit (`sim hash`, FNV-1a over the doubles): RC (AoE 1.33), 555 astable, inverter ring,
7490 BCD counter, Wien bridge, Schmitt relaxation oscillator, SCR, Johnson counter, Arduino
serial echo, Z80 mini, 6502 Ben Eater.

- First pass: 9 of 11 identical. The 7490 and the Johnson counter differed **even between two
  skips**: flip-flops draw a random start state with `std::rand()` when noise is off
  (`flipflopbase.cpp`), and `rand()` was never seeded per run (also random WaveGen, dynamic RAM).
- With a **run seed** (`std::srand` at each start; `settings runSeed`, `sim status` → `seed`):
  **11 of 11 identical**, the three MCU circuits included. So the frame boundaries, the
  speed and fast run don't change what is computed; the seed and the inputs are all a replay
  needs.

**Replay speed** (a 5 s skip from stopped, this M1) [SIMULATED]: 3× real time (Z80) to 40×
(RC, 7490); 555, Wien, Schmitt, Arduino 7–10×. Going back by replaying from the start
costs t / (3…40): instant for the slow speeds used to watch a circuit (1 min at 1 ms/s is
60 ms simulated), 1.5–20 s after a minute at real time.

**Consequence for the plan**: replay from the start needs no state API at all, only the
seeds and the input journal, and it works for **every** circuit, MCUs and script parts
included (they are deterministic too). Keyframes (§4.3, the state API) then only make it
faster. Proposed order:

- **2a. Replay from the start.** Run seed and noise seed kept per run; the input journal
  (§4.3) for live property changes (generic, at `setPropStr`) and the interactive parts
  (switch, push, DIP switch, keypad, variable sources, clock button), each stamped with the
  frame time at which `updateStep()` applies it; an input the journal can't record (a part
  without the hook, a real serial port) ends the history, as an edit does. ← and ⏮ replay to
  the target as a skip (Esc cancels, progress in the time label); `sim skip --back`,
  `sim history` (span, journal), `sim history check` (round trip with `sim hash`).
- **2b. Keyframes** with the state API (§4.5) for the common parts, to make ← take ≤ 50 ms;
  a circuit with a part that has no state API keeps 2a (slower, still exact).

### 4.8 Keyframes proven on board_mains (2026-11-21) [SIMULATED]

Decided with the user: keyframes first (replay from the start is too slow for `board_mains`:
it runs at 25–60 ms/s, so going back from 10 s would replay for 3–7 minutes), and the cost of
taking them must be proven on `board_mains` before anything else.

**What was built** (branch `feature/sim-history`):
- `StateIO` (`simstate.h`): one `simState( StateIO& )` per class reads or writes its fields, the
  same list both ways; raw bytes straight into a preallocated arena, no allocation per keyframe;
  a marker before each element catches a save and a load that drift apart. `SIM_STATE( Class )`
  declares a class covered; a subclass that doesn't is reported missing.
- Generic layers: the simulator core (time, non-linear budget, speed-lock factor), every node's
  voltage and stamped values (admittances, currents: the matrix is rebuilt from them), the
  event queue in order (element index + time), and the **AngelScript globals** of script parts
  (numbers, bools, enums, strings, read through the module API: no per-script code).
- Per class: the 27 classes of `board_mains` (R, C, L, diode, zener, junction cap, varistor,
  fuse with its heat model, switch, WaveGen, LED strip, ammeter, battery, current source,
  MOSFET, op amp, MCU pins and ports, the scripted MCU and core, subcircuit, scope).
- `SimHistory`: a ring of keyframes in one arena (256 MB default, `settings historyBudget`),
  one per frame taken in the GUI timer after `holdWorker()` (a solved step; a frame whose step
  isn't solved is skipped), restore = the last keyframe at or before t.
- CLI: `settings history` (on by default since going back was built), `sim history` (count, span, bytes,
  capture time mean/max, missing classes), `sim history sizes` (bytes per class),
  `sim history check [--span] [--points] [--back] [--keep CLASS]`, `sim hash`, `settings runSeed`.

**Rule found: display state is not in a keyframe.** Fields summed or reset at each GUI frame (LED
brightness, the scope's measurements and trace, the coil colour) depend on when frames fall, not
on the simulation, and never feed the circuit: they carry on from their present value. Two
reports were frame-dependent and are fixed: the diode's and LED's 100 ms current average
(`current_avg`, OVER) was advanced to each frame's time, splitting the integration differently
in each run; the stored average now follows the solved steps only, and frames and readers
compute it up to now without storing it.

**Results** (`tests/regress/test_simctl_history.py`, and a proof script run on this M1):

| | Measured |
|---|---|
| Keyframe | 17.6 KB (34 MCU pins 5.5 KB, 21 junction caps 3.4 KB: mostly constants; can shrink) |
| Taking one | 15 µs mean, 34 µs max, every frame (20/s): 0.03 % of the 50 ms frame |
| Speed, history off vs on | 42.97 vs 42.94 ms/s over 6 × 10 s each, alternating: −0.07 % (sd 0.2–0.5 %, noise); a second run −0.11 % |
| Memory | 350 KB/s of wall time: 256 MB holds 12 min, i.e. 30 s of `board_mains` time at 43 ms/s; 10 s back = 70 MB |
| Restoring one | 7–15 µs |
| Round trip (`sim history check`) | at 26 ms (start-up, 10 ms back), 198 ms (40 ms back), 445 ms (200 ms back), 589 ms (5 ms back): at 5 checkpoints each, every node voltage **and** every element's saved state identical to the bit |
| Negative control (`--keep`) | keeping the inductor, diodes, zener, varistor or the script cores at their present state: node voltages differ; keeping the fuse, meters, LED strip, op amp, junction caps: their state differs. So the check sees state that is not restored |

**Going back (built 2026-11-21)**: ← / ⏮ / `sim skip --back` restore the last keyframe at or
before now − interval and replay the rest as a skip (fast run), then pause; one solver step back
(`sim step --back`, the "1 step" interval) replays to now − 1 ps, reads when the last step ran
(`Simulator::lastEventTime`) and replays again onto it. Keyframes after the restored one are
dropped (the future is recomputed as it runs on). Measured on `board_mains` [SIMULATED]: back
10 ms lands on the earlier time with the same state hash it had there, forward again gives the
same hash as the first time, steps back land on the earlier 50 ns step times.

- **Inputs need no journal** while there is a keyframe every frame: a click sets a part's
  pending fields (e.g. `m_closed`, `m_changed`), which are saved, and `updateStep()` applies them
  right after the frame's keyframe, so a replay applies them at the same time. A CLI command that
  changes the running circuit (`comp set`, `ui click`, `serial send`, …) takes a keyframe right
  after it (`CliApi::exec`), as it acts between frames. A property changed in the GUI dialog
  between two frames is the one case still missed (up to one frame) — the journal of §4.3 is
  for that, and for thinned keyframes.
- `eElement::stateRestored()` after a load: a switch's button follows the restored contact; a
  scope channel drops its samples after the restored time (the trace is cut back).
- An edit that adds or removes an element during the run ends the history for the run
  (Messages: "History: the circuit changed during the run …").
- History is on by default (a user setting, `Simulation/history`); a circuit with a part
  without state code keeps running as before, and ← says which parts.

**More part types (2026-11-21)** [SIMULATED]: state code for every non-MCU part found in the 773
circuits of the repos (books, YouTube, led_driver, samples, the shipped examples, test circuits):
BJT, JFET, SCR, triac, diac, UJT/PUT, voltage and current regulators, the logic family
(`IoComponent` with its queues of pending outputs, gates, buffers, flip-flops, latches,
counters, mux/demux, shift registers, BCD decoders, adders), memories (`ioVector` of the RAM),
the 555, relays, push buttons, DIP switches, keypad, potentiometers and sensors (LDR,
thermistor, RTD, strain gauge), the transformer and its coils, sources, rails, clocks, probes,
voltmeter, frequency meter, logic analyzer, lamp, DC motor, stepper, LED displays and matrices,
HD44780/SSD1306/PCD8544/KS0108/ILI9341 displays (their RAM), WS2812, MAX72xx, I2C parts and the
DS1307/DS1621/DHT22/SR04/KY040 modules, and the **noise engine** (each source's random
generator and normal distribution, copied as bytes, so a noisy run replays the same numbers).
Most field lists were generated from the headers (plain values, fixed arrays, members with
their own state) and reviewed; `sim history check` over all the circuits found what was missing.

- Found that way: the models that publish exact pin currents (`ePin::trueCurrent`: BJT, JFET,
  current regulator, op amp) keep them on the pins, which keyframes didn't hold; the noise
  read them, so a noisy BJT circuit diverged after a restore. `eElement::pinCurrents()` saves
  them. More display state left out: the lamp's brightness, the DC motor's angle and speed, the
  frequency meter's averaged period (all summed or reset per GUI frame).
- A skip that lands takes a keyframe where it pauses: a skip shorter than a frame left none,
  and the places skipped to are quick to come back to.
- **Result**: 659 of the 773 circuits go back bit for bit (`sim history check`: 6 × 1 ms run,
  back 4 ms, 3 checkpoints, every node and every element's state identical), against 143
  before. `tests/regress/test_simctl_history_parts.py`: 15 shipped examples covering the new
  classes, plus a BJT oscillator with noise on. 106 circuits are refused: all but one hold an
  MCU (AVR, PIC, 8051, …: their peripherals and cores are the next step); one has a
  `SubPackage` (the package editor).
- **Known limits** (circuits that run but can't replay exactly; `sim history` says why where it
  can):
  - A **meter's output pin** (ammeter, voltmeter `outnod`) is set once per GUI frame
    (`Meter::updateStep`), so the circuit depends on when frames fall; a replay differs where
    that output feeds something (2 examples). Would need the output updated at fixed simulated
    times.
  - **Steps that don't converge** (the UJT relaxation oscillator of Duncan 56.4, the centre-tap
    rectifier of Duncan 40.3): a step that iterates past its limit can be cut by the 50 ms
    wall-time guard (`breakStep`), so the run itself isn't repeatable; where no step is ever
    complete at a frame, no keyframe is taken (`sim history` error: "the step was not solved").
  - The **audio output** is paced by the sound card's buffer: refused (`hasSimState` false).
  - Scherz 12.57b (switch debouncer with the T_AND ternary subcircuit) lands in another state
    after a replay, unexplained so far.

**MCUs (2026-11-21)** [SIMULATED]: the AVR and PIC families. The core saves its own fields
through `CoreBase::coreState()` (PC, return address; the PIC mid-range core also W, bank, its
8-level stack and pointer; the registers are in the data memory `eMcu` already saves);
`Interrupts::intState()` saves the global flag, the active / pending / running lists and each
interrupt's flags and module callbacks as indices; the peripherals (timers, OC/IC/CCP units,
ADC, comparator, EEPROM, watchdog, sleep, SPI, TWI, USART transmitter/receiver, internal
oscillator, Vref, DAC, pins) were generated like the parts (their register bit masks are
configuration). Found by the check:
- A port's input bits (`McuPort::m_pinState`, read as PINx) are state, and ports aren't
  elements: `eMcu` saves them (`portState`). Without it the Arduino scope example read a
  different PINC after a restore.
- A node's list of elements called on a voltage change (`eNode::m_voltChEl`) changes during a
  run: a DHT22 stops listening while it answers. It is saved, in order (element pointers: they
  live for the whole run).
- More display state: the logic analyzer's frame counter.

The AVR circuits completed (2026-11-21): the serial terminal (its typed bytes waiting to be sent),
the scripted I2C and SPI modules (their script globals are the script CPU's), and the real
serial port refused (data from outside can't be replayed). Found: a **potentiometer set from
the simulation** (a script's I2C byte, a linked part) only took its new value at the next GUI
frame, so the circuit depended on when frames fell; it is now applied at once (`applyRes`, the
label still follows at the frame). And a dial is GUI state that signals only a change: after a
restore it still showed the later value and swallowed the same value set again by the replay,
so `stateRestored()` moves the potentiometer's and the variable resistors' dials back. All 51
AVR circuits of the repos now go back bit for bit, except those with a real serial port or
the audio output (refused).

The other families (2026-11-21): the **8051** (its core is an element; found: the instruction
bytes already fetched, `m_readOp`, are state), the **Z80** (its register structs are plain
values; `Z80RegPair` only refers into `regs[]`), the **ZX Spectrum ULA** (with its 448 × 312
screen buffer), the **6502** (the routine of the current cycle is a member-function pointer,
valid for the run, copied as bytes), the **scripted display** (its picture), and the package
editor (`SubPackage`). The standalone CPUs (Z80, 6502, ULA) save their PC themselves, as no
eMcu does it for them. MCU modules that are not elements (USART register front ends, ports)
get `McuModule::moduleState()`, called by `eMcu` (the 8051 USART's baud counter). The logic
analyzer's channels are display state like the scope's: not in a keyframe, their trace cut
back on a restore (`LaChannel::stateRestored`).

After these, every circuit of the repos is covered except those with a real serial port or the
audio output (8, refused on purpose): **757 of the 773 go back bit for bit** (143 before the
part state code); the other 8 are the known limits above. `test_simctl_history_parts.py`: 44 checks, 21 circuits. 

**Circuits that don't go back yet** (2026-11-21, `sim history check` over the 773 circuits of the
repos: 6 × 1 ms run, back 4 ms, 3 checkpoints). Paths from `~/perso/electronics/`.

Refused on purpose (← names the part; the circuit runs as before):

| Circuit | Part | Why |
|---|---|---|
| `SimulIDE/resources/examples/Micro/Arduino/Serial_Port_connection/analog_serial.sim1` | SerialPort | a real port: data from outside can't be replayed |
| `SimulIDE/resources/examples/Micro/Arduino/Serial_Port_connection/barGraph_serial.sim1` | SerialPort | same |
| `SimulIDE/resources/examples/Micro/Peripherals/serial port/arduino_serial_echo.sim1` | SerialPort | same |
| `SimulIDE/resources/examples/Micro/mcs-65/micro-kim-1/micro-kim-1.sim1` | SerialPort | same |
| `SimulIDE/resources/examples/Micro/Arduino/arduino_music/arduino_music.sim1` | AudioOut | paced by the sound card's buffer (wall time) |
| `SimulIDE/resources/examples/Micro/Pic/16F88x/16F886_Song/song.sim1` | AudioOut | same |
| `SimulIDE/resources/examples/Outputs/Audio/audio.sim1` | AudioOut | same |
| `SimulIDE/resources/examples/Outputs/Audio/resonator.sim1` | AudioOut | same |

Go back, but the replay differs (what differs, then the cause):

| Circuit | Differs | Cause |
|---|---|---|
| `SimulIDE/resources/examples/Active/BJT/npn-curve.sim1` | ammeter outputs `Amperimeter-49/114-outnod`, `Oscope-8-Pin1` | a meter's output pin is set once per GUI frame (`Meter::updateStep`) |
| `SimulIDE/resources/examples/Active/triac.sim1` | ammeter output `Amperimeter-4-outnod` | same |
| `led_driver/sim/board_mains_reference.sim1` | ammeter output `Amperimeter-34-outnod` | same |
| `books/duncan/fig56_4_ujt_relaxation_oscillator.sim1` | the UJT's two BJTs, `Capacitor-5` | steps that don't converge (100 000 iterations), cut by the 50 ms wall-time guard: the run itself isn't repeatable |
| `SimulIDE/resources/examples/Micro/Pic/16F88x/16f887_CCP/887_ccp.sim1` | logic analyzer input pins (once; node voltages the same) | the first run after opening differs from later runs (below) |
| `books/scherz/fig12_57b_switch_bounce_example.sim1` | diodes, zener and buffer inputs of the T_AND ternary subcircuit (another steady state) | unexplained: the run is repeatable, so some state is missing |

No result:

| Circuit | Why |
|---|---|
| `books/duncan/fig40_3_centre_tap_rectifier.sim1` | diode D1 never converges, so no step is complete at a frame and no keyframe is taken (`sim history`: "the step was not solved") |
| `books/scherz/fig3_91e_coax_highpass.sim1` | the check took over 120 s (a slow circuit); not known to differ |

**Parts that break going back** (2026-11-21: each of the 569 items of the component library
placed alone in a running circuit, then `sim history` → `missing`; library item names as in
`lib list`). A circuit with one of these refuses ← / `sim skip --back` and names the class; it
runs as before.

State code added for all of them the same day (they were in none of the repos' circuits, so
the survey of the circuits had missed them): ADC; DAC (DAC, DAC7621, DAC8562); DS18B20; the
scripted **Function** (Function, 74HC40102, 74HC40103, 74XX381, 74XX382, AD7224, CD4009, CD4010,
TL494: its script globals, as the script CPU's, so it is refused if a global has a type
`scriptGlobals()` doesn't handle); Header and Socket (`ConnBase`); I2CMaster (its transfer: the
bytes to write, read and their ACKs); KY023 joystick; Servo (not its position: integrated per
GUI frame, display); TouchPad. Each is checked alone in a running circuit
(`test_simctl_history_parts.py`, 86 checks).

Refused on purpose (the only library items left): `SerialPort` (a real port) and `Esp01` (real
TCP sockets): data from outside can't be replayed; `AudioOut` (paced by the sound card's
buffer).

**Telling the user** (user, 2026-11-21): when a circuit that can't go back is opened, Messages
gets a warning naming each part and why ("Going back in time is off for this circuit: AudioOut ×1
(paced by the sound card, in real time). The circuit runs as before; ← and ⏮ work in circuits
without these parts."); each ← or ⏮ repeats it, and `sim skip --back` answers the same. The ⏮
button is greyed out with the reason in its tooltip (also when history is off in the settings),
checked again at each start (`CircuitWidget::updtBackAvail`). The reasons come from the parts
(`eElement::noSimStateWhy()`: a real port or network, the sound card, script variables of a type
keyframes can't hold; otherwise "no state code yet"), gathered by `SimHistory::blockers()`.

Covered, but the replay is not exact:

| Part | When |
|---|---|
| Ammeter, voltmeter (`Meter`: Amperimeter, Voltimeter) | their output pin (`outnod`) takes the shown value once per GUI frame, so its value depends on when frames fall |
| Any part whose model doesn't converge in a circuit (seen: the UJT in Duncan 56.4, a diode in Duncan 40.3) | a step cut by the 50 ms wall-time guard makes the run itself unrepeatable; no keyframe while no step is complete |

Found on the way: the **first run after opening** `board_mains` differs from later runs (two
skips of 200 ms after the first: same hash; the first: another), so some part keeps state
across a stop and a start. It doesn't touch keyframes (they restore within a run); to chase.

## 5. Phase 3: saved snapshots and triggers

### 5.1 Snapshots

A snapshot is one keyframe written to a file, so a state can be opened and inspected later:
the meters, part states, current flow and charge views show that moment, and the simulation
can be resumed from it.

- Only for circuits that have a history (§4.6).
- File: `<circuit>.snap/<name>.snap1` in a folder next to the `.sim1` (D10), holding the
  keyframe, the simulated time, the simulation settings, a hash of the circuit (a snapshot of a
  different circuit is refused, or opened with a warning), and a note.
- `sim snapshot save [name]`, `sim snapshot open <name>`, a Snapshots list in the GUI.
- Uses: show a fault state to someone, skip a long start-up (a supply warming up, an MCU
  booting) every time, start a regression test from a known state.

### 5.2 Triggers

A trigger watches the simulation and acts when its condition is met, like a scope's trigger.
Shares the trigger engine with the scope redesign (specs/scope-redesign.md,
specs/scope-trigger-level.md).

- **Conditions**: a node voltage or a part's current crosses a level (rising, falling, either),
  a logic level or edge, a part state change (an overstress or rating from docs/PART_STATES.md),
  a non-converged step (`NlReport`), an MCU breakpoint or register value, a time.
- **Actions**: pause, save a snapshot, or both; with the history, the snapshot can be taken a
  set time **before** the trigger (pre-trigger), as a scope shows the signal before its trigger.
- **Modes**: single (once, then disarmed), normal (every time, with a holdoff and a maximum
  count).
- Saved with the circuit; listed in the CLI (`sim trigger add|list|del`).

## 6. Order of work

1. Phase 1: key filter and focus (3.1–3.2), skip interval and skip forward (3.3–3.4), editable
   speed and skip boxes with ⏮ ⏭ (3.5), CLI (3.6), the settings marks and Messages lines
   (3.8). One PR.
1b. Speed lock (3.9). One PR. Built (`feature/sim-speedlock`).
2. Phase 2: state API on a first set of parts with `sim history check`, keyframes, replay and
   skip back; then the remaining circuit part classes (not MCUs, §4.6).
3. Phase 3: snapshot files, then triggers (with the scope redesign).

## 7. Risks

- Two owners of the same keys (canvas editor and simulation, D1): the user must see which one
  has them (§3.1); Esc on an idle canvas gives them back (D11). A quick Esc, Esc, Esc from a
  selected part stops the simulation on the third press.
- Phase 2 depends on every part class saving all of its state; a forgotten field gives a
  replay that slowly drifts. The round-trip check is what keeps this honest.

## 8. Open points to check in the code

- Why the canvas keeps focus today at start, and which widget takes it (the run button?).
- The range `Cli::setSimSpeed()` accepts, and how the real-speed override behaves in an
  editable combo.
- Whether `sim run --for` runs at the set speed or as fast as it can (decides how §3.4 reuses it).

## 9. Decision log

| # | Question | Decided (user, 2026-11-21) |
|---|---|---|
| D1 | While running, do Space / Esc / arrows go to the simulation even when the canvas has focus? | **No.** With the canvas focused they work as today (keyboard editor); they control the simulation only when the canvas has no focus |
| D2 | macOS media keys? | **Dropped.** F5 = start/stop, F6 = pause/resume (as today) |
| D3 | Space and Esc during a long skip: cancel the skip (stay paused); Esc twice stops? | yes |
| D4 | What does → do when the simulation is **stopped**? | **(a)**: start it, skip one interval from t = 0, end paused |
| D5 | ↑/↓ factors: none = one 1–2–5 notch, Shift ×5, Option ×10, Shift+Option ×100; → / ← take the same modifiers | yes |
| D6 | Skip interval saved with the circuit | yes |
| D7 | A skip runs as fast as possible, not at the set speed | yes |
| D8 | ⏮ ⏭ buttons in the toolbar | yes |
| D9 | Resuming from the past discards the recorded future (no branches) | yes |
| D10 | Snapshots in a folder next to the circuit | yes |
| — | Going back: full snapshot per step, deltas, or keyframes + replay (§4.2)? | **a snapshot every frame** + replay |
| D12 | History and snapshots for all parts, or limited? | **limited to circuits**: no MCUs, script parts or outside links (§4.6) |
| D11 | Once the canvas has focus while running, how do the simulation keys come back without stopping? | **Esc on the canvas** when nothing is in progress and nothing is selected (§3.1) |
| D13 | Must the simulation stop while simulation settings are edited? | **No**: mark "next start" settings, log changes to Messages, journal them for the history (§3.8) |
| D14 | Speed lock upper limit | a **"max. reactive step" setting**, default 100 × the reactive step |
| D15 | Skips and `sim run --for` ignore the speed lock (factor ×1) | yes |
