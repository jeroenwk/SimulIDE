# SimulIDE command line interface

SimulIDE can be driven entirely from the command line: build and edit circuits,
run simulations, read meters and scopes, press buttons, load firmware, talk to
UARTs, compile and debug code, and change settings.

The CLI talks to a running SimulIDE over a local socket:

- **Live:** the SimulIDE GUI listens automatically. CLI changes show up in the
  open window, and the GUI's Undo/Redo works on them.
- **Headless:** `simulide --headless` runs with no window, for scripts, CI and
  AI agents. Any dialog a command would open is closed automatically, and its
  text is returned as a note.

```sh
simulide --headless --socket demo &           # or just open the GUI
simulide cli --socket demo comp add Resistor --label R1 Resistance=1k
simulide cli --socket demo sim run --for 10ms
simulide cli --socket demo read R1
```

## Starting SimulIDE

```
simulide [circuit.sim1]                       GUI, listening on the default socket
simulide --headless [--socket NAME] [file]    No window; exits on "quit", or after 30 min without a request
         [--idle-exit TIME] [--keep]          idle limit (90s, 10m, 2h; 0 = never); --keep = never
simulide --no-server                          GUI without the CLI socket
```

- A headless server quits by itself after `--idle-exit` (default 30 min) without a CLI
  request, counted from the last request or reply and never while one is still running
  (a long `sim run --for`). Servers started for a session or a script used to outlive it
  by days. Its circuit is gone with it: a script that pauses longer passes `--keep`.

- The default socket name is `simulide-cli`. Set `SIMULIDE_SOCKET` or pass
  `--socket` to both the server and the client to run several instances.
- The socket is a local (Unix domain) socket that only your user can access.
- If the socket is already in use by a running SimulIDE, a second instance
  doesn't take it over.
- On macOS the binary is `simulide.app/Contents/MacOS/simulide`.

## Client

```
simulide cli [--socket NAME] [--json] [--timeout SEC] <command> [args]
simulide cli [options] --script FILE      Run commands from a file
simulide cli [options] -                  Run commands from stdin
```

| Option | Meaning |
|---|---|
| `--json` | Print the raw JSON reply instead of text |
| `--timeout SEC` | Give up waiting for a reply (default: wait forever) |
| `--keep-going` | In script mode, continue after a failing command |

- **Script mode:** one command per line. Lines starting with `#` are comments.
  Quoting works like a shell (`'...'`, `"..."`, `\`). The script stops at the
  first error unless you pass `--keep-going`.
- **Exit codes:** `0` ok, `1` command error, `2` cannot connect.
- **Output:** errors go to stderr as `error: ...`, and notes (closed dialogs,
  warnings) go to stderr as `note: ...`.
- **While the simulation runs**, every command first lets the solver finish the time
  step it is working on and runs between two steps, so `read`, `probe` and the rest give
  a solved step (Kirchhoff's laws hold), never a Newton iteration in progress. The
  solver then goes on at once. A step that needs over 50 ms of wall time is still cut
  short (`tests/regress/test_settled_reads.py`).

### Arguments

- **Components** are named by uid (`Resistor-3`, see `comp list`) or by label
  (`R1`) when the label is unique.
- **Pins** are written `component.pin`, where the pin is:
  - its id (`R1.lPin`, `G.Gnd`),
  - its label (`U1.PB5`),
  - or its index (`R1.0`).
  `comp get <comp>` lists the pins.
- **Properties** are set as `name=value`, with case-insensitive names.
  - Values accept engineering notation: `4k7`, `4.7k`, `10u`, `10uF`, `1M`, `100n`.
  - A wrong unit is an error (`Resistance=5V`).
- **Times** are written with a unit: `500us`, `10ms`, `2s` (also `ps`, `ns`).
- **Options:** `--key value` or `--key=value`. Everything after `--` is
  positional, so text starting with `--` can still be sent.

## Commands

`simulide cli help` lists every command and `simulide cli help <command>`
shows one.

### Circuit

| Command | |
|---|---|
| `circuit new [--discard]` | Start an empty circuit |
| `circuit open <file.sim1> [--discard]` | Open a circuit |
| `circuit save [file.sim1]` | Save it (to its current file, or a new one) |
| `circuit info` | File, modified flag, counts, simulation settings, and `matrix`: each group of connected nodes the solver handles, with its size and how many times it was factorized and solved since the simulation started (a solver load check) |
| `circuit export` | The circuit as `.sim1` XML |
| `circuit fonts` | Convert Text components to the default fonts (Inter; JetBrains Mono for monospace names), one undo step; returns `converted` (same as the canvas right-click menu's **Convert Fonts to Default**) |
| `undo`, `redo` | Same as Ctrl+Z / Ctrl+Y |
| `quit [--discard]` | Close SimulIDE |

Commands that would throw away unsaved changes refuse unless you pass `--discard`.

### Library and components

| Command | |
|---|---|
| `lib list [--search TEXT] [--category CAT]` | Everything that can be added: built-in parts, MCUs, ICs, boards |
| `lib select <name>` / `lib select --none` | Select a category or component in the component list (expands and scrolls to it), or clear it; `capture window library` grabs the list |
| `comp add <name> [--at x,y] [--label L] [--rot DEG] [prop=value ...]` | Add a part. Prints its uid and where each pin is (`pos` x,y, the `side` it points to, and `hidden: true` for a pin the part does not draw in its current mode, e.g. a WaveGen's `gndnod` unless Bipolar) |
| `comp list [--type TYPE]` | Components in the circuit |
| `comp get <comp> [--all]` | Properties (with units, or the allowed options for choice properties) and pins (with position, connections, and voltages while running). `--all` adds hidden properties, marked `hidden` |
| `comp set <comp> prop=value ...` | Change properties. `label=` renames. `idLabPos=dx,dy` / `valLabPos=dx,dy` move the name / value label (offset of its top-left corner from the part's position; switches the part's labels to manual placement). A choice property takes one of the options `comp get` lists, matched without case and stored as listed (`Wave_Type=triangle` sets `Triangle`), or its index; anything else is refused with the options (`comp add` too). When the part stores another value than asked (a setter that clamps: Battery `Voltage=-9` stores 1e-12 V), the reply has a `note` with both (`comp add` too). Undoable |
| `comp move <comp> <x,y>` | Move (snapped to the grid) |
| `comp rotate <comp> [cw\|ccw\|180]` | Rotate |
| `comp flip <comp> <h\|v>` | Flip |
| `comp remove <comp> ...` | Remove parts and their wires |
| `comp link <comp> [target ...] [--clear]` | Links of a linker part (scripted core, MCU): the parts it drives, in order (index 0, 1, ... of `component.setLinkedValue` in a script). Targets are appended, `--clear` removes all first, no target lists them. Like the GUI's Link mode, not undoable |

- `comp add` is atomic: if a property is invalid, nothing is added.
- `comp add`, `comp list` and `comp get` give each part's drawn outline as
  `body` [x, y, w, h] in scene coordinates (pins stick out of it) and, under
  `labels`, every visible label (`kind` name or value, `text`, `rect`). Layout
  checks use them (`electronics/.claude/skills/create-circuit/layout_check.py`).
- Hidden properties (shown by `comp get --all`) can be changed with `comp set`
  too. For scope settings, `scope set` is easier.
- Adding or removing parts stops a running simulation, as it does in the GUI.
- `comp add` also takes a device from the open circuit's `data/<name>/` folder
  (`<name>.mcu`: a scripted or MCU chip; `<name>.sim1` with a package: a
  subcircuit), as SimulIDE loads them inside subcircuits. That is how the inside
  of a datasheet part is built (`electronics/components/README.md`).
- Parts in the user data folder (`settings set userPath DIR`, then restart) are
  listed by `lib list` like built-in ones.

### Wires

| Command | |
|---|---|
| `wire add <pinA\|@x,y> <pinB\|@x,y> [--via x,y,...]` | Connect two pins, or a pin and a point on a wire (a new junction there). A pin that already has a wire gets a junction on its net. The route goes through the `--via` points and is always horizontal/vertical |
| `wire list` | Wires and the pins they join |
| `wire remove <wire> ...` | Remove wires. Prints the removed uids and any merged wires |
| `wire style <wire> ... [--color C\|none] [--band C\|none] [--opacity 0..100] [--width W\|default]` | Wire colours ([specs/wire-colors.md](../specs/wire-colors.md)): line colour, and a translucent band behind the wire (default 6 px, 50 %). One undo step; without options prints them. `wire list` shows `color`, `bandColor`, `bandWidth` when set |
| `wire current [wire ...]` | Current through wires (A), signed from the `from` pin to the `to` pin. Works paused. `flag` marks shared (`approximate`) or non-conserving (`inconsistent`) results. With currentFlow on: `level` and `dots` (`forward`, `backward` for a moment after going back in time, `still`) |

- `comp move`, `comp rotate` and `comp flip` also print the new pin positions.

Connecting to a pin that already has a wire inserts a junction node on that
wire, as clicking on a wire does in the GUI. The junction goes in the middle of
the wire segment next to that pin, on the 8 px grid. When that wire has no
straight segment of 16 px or more (a short stub to a junction), the junction
goes on the nearest such segment of the same net instead, with a note saying
where. To put it somewhere
else, name a point on the wire instead of the pin: `wire add U1.Trg @160,64`
joins the wire that passes through 160,64. The point can be anywhere along a
wire, corners included. It can't be where two wires cross (that's ambiguous)
or at a wire's end (connect to that pin instead).

**Current flow:** `settings set currentFlow true` shows moving dots on the
wires (conventional current direction; the toolbar button does the same).
Faster and brighter dots mean more current, relative to the largest current
in the circuit (log scale). `settings set currentRange 2` sets how many
decades below the largest current are shown (1..6, default 2; fewer = more
contrast). The smallest current shown at all is adjustable: `settings set
currentMin 100n` (default `1u`). `wire current`
gives the numbers without the dots.

**Reopen last circuit:** the GUI reopens the circuit that was open when it was
closed, unless a file is given on the command line or a crash backup is
restored. `settings set reopenLast false` turns it off (Settings > App). Headless
instances never reopen a circuit, and never change which one the GUI reopens.

**Circuit description:** a circuit file can carry a description (Markdown): what
the circuit shows, how to use it, what to expect. It is saved as `<description>`
after the `<circuit>` header. When the circuit is loaded it is printed in white
in the Messages tab, just before the "Circuit Loaded" line, and rendered in the
**Description** tab next to it. Edit it in that tab (Edit / Done) or with `circuit description --set TEXT`, `--file notes.md` or
`--clear`; `circuit description` alone reads it. Like any edit, save the circuit
to keep it.

**Scenario scripts:** the bottom pane's **Scripts** tab runs Python scripts that
drive the window through this CLI (usually with `tools/simctl.py`), for example a
bench scenario with checks. It lists the scripts next to the open circuit (its
folder and a `scripts/` subfolder: `.py` files that use `simctl` or carry the line
`# simulide-script`); **Open…** adds any other file. The left side shows the
script's description, its module docstring (or its leading `#` comments); the
right side its live output. **Run** starts it with `python3` (arguments in the
field next to it), **Stop** ends it, cancels a `sim run --for` it was waiting on
and pauses the simulation. One script runs at a time. The script gets
`SIMULIDE` (this binary) and `SIMULIDE_SOCKET` (this window's socket) in its
environment; `simctl.Sim()` uses both. From the CLI: `script list`,
`script run file.py [--args="--fast"] [--wait]`, `script stop`, `script status`
(state, exit code, run time, output). Write `--args=...` with `=`: a value that
starts with `--` would otherwise be read as an option.

**Interface theme:** `settings set uiTheme system|light|dark` (Settings > App >
Interface). `system`, the default, follows the light or dark mode of the computer
and switches with it (live on macOS; elsewhere the mode at start). It covers the
windows and panels; the circuit canvas keeps its own colours (Settings > Circuit >
Colors). The code editor stays light in both modes.

**Instruments and their leads:** wires attached to a scope or meter input are
drawn as leads: thicker, in the channel colour (scope CH1–CH4) or red (+) and
grey (COM, scope ground), adjusted to stay readable on the canvas colour. A
tunnel wired to an instrument input continues the lead; the rest of its group
does too when it is a single tunnel (where the lead clips on), not when the
group also carries a signal through the circuit. The lead ends at the first
junction. `settings set instruments true|false` (on by default; the toolbar
button with the scope screen does the same) shows or hides every scope, logic
analyzer, voltmeter, frequency meter and probe, with its leads, lead tunnels,
the grounds only they use, and their junction dots. Hidden instruments keep
measuring (`scope data`, `read`). Ammeters stay: they are in series.

**Part states:** `settings set partStatus true|false` (on by default; the
toolbar button with the red chip does the same) shows the states parts report
while the simulation runs. A scripted core reports with
`component.setStatus("HICCUP", ST_FAULT)`; the levels are `ST_OFF` (grey),
`ST_ON` (green), `ST_WAIT` (amber) and `ST_FAULT` (red).
- Each reporting block gets a badge above the part ("supply HICCUP"); waiting
  and fault badges are filled.
- The part's outline takes the colour of its most severe state.
- A state shorter than a frame that was more severe than the current one stays
  on screen for 3 frames, so a short fault is seen.
- `status <comp>` lists the states now and every change with its simulated
  time; `read <comp>` includes the current ones as `status`.
- Built-in parts report their operating state (`docs/PART_STATES.md`): BJTs
  **OFF** (grey, cut-off: VBE < 0.45 V), **ACTIVE** (green, amplifying) and
  **SAT** (amber, saturated: VCE < 0.2 V), and **BREAKDOWN** (red) past a
  BVceo or BVebo that is set; MOSFETs **OFF** / **ACTIVE** /
  **OHMIC**; SCRs, triacs and diacs **OFF** or **ON** (amber, conducting); op
  amps **LINEAR** / **SAT+** / **SAT-** (amber, at a rail); voltage regulators
  **REGULATING** / **DROPOUT** (amber) / **OFF**; diodes **OFF** / **ON** /
  **BREAKDOWN** / **OVER** (the averaged current above Max Current, IF(AV)) /
  **SURGE** (a peak above Surge Current, IFSM, held 100 ms), zeners **REGULATING** in breakdown, LEDs **OFF** /
  **ON** / **OVER** (the averaged current above Max Current, IF) / **PEAK** (a step above Peak
  Current, IFP, held 100 ms). Diodes and LEDs have Show State off by default (there are
  usually many): turn it on for the ones to watch. The 555 reports **HIGH** /
  **LOW** / **RESET**, relays **ENERGISED** / **RELEASED** (the coil), analog
  muxes **CH n** / **DISABLED**. Each boundary has a little hysteresis. The state
  is taken when each time step has converged, so the times are exact at any
  speed. `status` lists them, `read` gives the current one as `state`, and
  Messages logs them. A part's **Show State** property (`Show_State`) turns its
  badge and log off.
- MOSFETs and JFETs report **OFF** (grey, VGS below
  turn-on or past pinch-off), **ACTIVE** (green, amplifying: the FET's
  "saturation" region, VDS > VGS − Vth) and **OHMIC** (amber, fully on, a
  resistor: VDS < VGS − Vth), the same way.

**Event log (Messages):** while the simulation runs, the Messages pane lists
what happens, with the simulated time, coloured like the part-state badges:
```
[   312.009 ms] IC1  supply: UVLO → ON  ·  pwm: IDLE → WAIT COMP
[   414.817 ms] IC1  fb: OK → OVP  ·  supply: ON → HICCUP  ·  pwm: RUN → STOP → IDLE
[   419.683 ms] C5  charged to 23.7 V
[     1.4017 s] IC1  repeats the same cycle of 5 changes every 340.234 ms (2.94 Hz): not listed further while it repeats
```
- Part states: every `setStatus` change, exact time; changes within 1 µs share a line.
- Capacitors whose **Log charge** property is on (`comp set C5 LogCharge=true`):
  charging, charged, discharging, discharged, above / back within the rated
  voltage. The capacitor records its voltage every 0.1 ms of simulated time,
  whatever the speed. A change is judged on 20 ms averages (mains ripple
  averages out) with a 3 % hysteresis; once seen, the recorded voltage is
  searched for when it really happened (the foot of the rise, the first point
  at the new level, ripple included), and that is the time and level logged.
  A "charged" line therefore appears about 40 ms of simulated time after the
  moment it names (two equal 20 ms averages are needed to call a level steady).
- A step the solver could not converge (the "NonLinear Not Converging" warning)
  always gets a red line: its time, the iterations, and the parts still
  iterating ("D1, D5, IC1.Mosfet-28, ..."). The step is then taken as it is
  and the next steps get at most 1000 iterations until one converges, then
  the full budget (`nlSteps`) again. `log --kind solver` lists them.
- Lines follow the toolbar: part states only while **Show Part States** is on,
  capacitors only while **Show Capacitor Charge** is on; with both off the log
  does no work. Switched on during a run, it starts from that moment.
- A sequence of changes that comes three times in a row is summarised in one
  line with its period, then not listed until it changes ("cycle ended after
  N repeats"); at most 20 lines per part per second, then "N more changes".
- Click a line to select its part. The **Part states** / **Capacitors**
  checkboxes above the pane (`settings set logParts|logCaps`) switch the lines
  off as well (greyed while their toolbar display is off); `log [--since T] [--kind part|cap] [--comp C] [--last N]` returns every
  event of the run, also those not shown (`shown: false`).

**Capacitor charge:** `settings set capCharge true` shows how far each
capacitor is charged (the toolbar button next to current flow does the same):
- a gauge bar beside the part, filling upwards with |V| / Vref;
- each plate coloured by its node voltage, grey at 0 V, the Animate high
  colour at +Vref and the low colour at −Vref.

`settings set capLabel off|volt|percent|both` adds live text beside it
(default `off`). The reference (100 %) is, in order:
1. `settings set chargeScale 400` (V, saved in the circuit; 0 = auto);
2. the largest source voltage (Fixed Voltage, Rail, Battery, Voltage Source,
   WaveGen; also inside subcircuits and while switched off);
3. with no source, the highest capacitor voltage since the start.

A capacitor's own `ChargeScale` property (V, 0 = the circuit's; in its
Properties panel as "Charge gauge 100 % at") overrides all three for that
capacitor's gauge, label and plate colours: `comp set C1 ChargeScale=20`.

**Inductors and transformers** (same switch and label setting, see
`specs/inductor-current.md`): a gauge beside each inductor and each
transformer coil fills with |I| / Iref, and the coil is coloured by its current
(Animate high colour from pin 0 to pin 1, low colour the other way; label
`volt` mode shows amps). Iref is, in order: the part's own `CurrentScale`
(A, `comp set L1 CurrentScale=0.1`), `settings set currentScale 0.1` (A, saved
in the circuit; 0 = auto), else the peak |I| of all inductors and coils since
the start (shared; parts with their own scale left out). An RL step therefore
reads 100 % during its first rise on Auto: set a scale (e.g. V/R) for that.
An inductor's `MaxCurrent` (rated current) turns its gauge red above it.
`read L1` gives `current` (signed, A), `level`, `iref`, `iref_source`
(`part`, `override`, `peak` or `none`), `rating` and `flag` (`overcurrent`,
`overflow`); `read T1` gives `i_P01`, `level_P01`, `i_S01`, ... per coil
(named like the pin at the coil's end) plus `iref` and `iref_source`. A
relay's coil shows nothing.

A capacitor's `MaxVolt` property (rated voltage) makes its gauge red and
blinking above it, and draws a tick at that level. `read C1` gives the
numbers: `voltage`, `level` (can exceed 1), `vref`, `vref_source`
(`capacitor`, `override`, `sources` or `peak`), `rating` and `flag` (`overvoltage`,
`reversed`, `overflow`).

**Colors:** `settings set colorTheme System|Classic|Light|Dark` picks a preset (Dark
is inspired by EveryCircuit: black canvas, hollow light grey parts, pale green
current dots; System is Light or Dark following the OS appearance, also when it
changes while SimulIDE runs, until another preset or a color is chosen); it reads
`Custom` once a color was changed.
`settings set canvasColor|wireColor|lineColor|textColor|bodyColor|flowColor|nameColor|valueColor|icBodyColor|icTextColor <color>`
sets the canvas, wires, component outlines (lines, pins, arrowheads), text
inside components (pin names), component bodies (parts drawn white by default),
current flow dots, and the name (R1) and value (470 Ω) labels.
`icBodyColor`/`icTextColor` color IC packages (chips, subcircuits, MCUs) and the
pin names inside them; the pin-1 marker is derived from both. At the default
IC body each kind keeps its classic shade (chips, subcircuits and MCUs differ
slightly); packages with their own background image or color keep it, and
logic-symbol packages keep their light blue only with the default body and IC
body colors, else they take the body color. Light draws ICs white with black
pin names; Dark makes them hollow like the other parts. Animate colors:
`animHighColor`/`animLowColor` (wires above/below 2.5 V), `outHighColor`/`outLowColor`
(output pins), `inHighColor`/`inLowColor` (input pins) and `openHighColor`
(open-drain output released). A color is `#rrggbb` or a name (`white`, `navy`); `default` restores
the classic one. All of it is in Settings > Circuit > Colors too, with a reset
button. Animate colors wires by voltage (red/blue) and overrides the wire color.
`capture circuit` uses the canvas color.

Routing:
- Where two consecutive points differ in both x and y, `wire add` adds a corner.
  The wire leaves its first pin in the direction the pin points, then turns at
  each corner. Without `--via`, if that would leave the first pin backwards or
  enter the second pin from behind (through its own part, e.g. a tunnel facing
  away from its target), the other corner is used.
- `wire add` adds a note when the new wire lies along another wire, or passes
  over a pin it doesn't connect to. Both look connected in a picture but are
  not. Use `--via` to route around.

Removing wires around a junction works as in the GUI:
- A junction left with two wires is dissolved, and the two merge into one new
  wire with a new uid. `wire remove` reports it under `merged`.
- A wire left alone on a junction is removed with it.
- When you remove several wires in one command, a merged wire made from wires
  you named is removed too, so `wire remove` of all the wires around a
  junction leaves nothing behind.

### Simulation

| Command | |
|---|---|
| `sim start` / `sim stop` | Power the circuit on or off |
| `sim pause` / `sim resume` | |
| `sim run --for <time>` | Run for an exact span of simulated time, then pause. Replies when done |
| `sim hash` | Bit-exact hash of every node voltage at the current time, with the time and the node count: two runs to the same time compare equal only when they computed the same |
| `sim history [sizes \| check …]` | Keyframes of the run (specs/sim-control.md §4.8, `settings history`): count, span, bytes, capture time (mean, max), classes without state support (`missing`). `sizes`: bytes per class. `check [--span T] [--points N] [--ago T] [--keep CLASS]`: from now, skip to N checkpoints recording the node and state hashes, restore the keyframe T before now, skip to them again: `ok` when all are identical; `--keep CLASS` is a negative control (that class keeps its present state), `differ` names the elements that differ |
| `sim status` | State, simulated time, target and actual speed; `skipping: true` while a skip is not done; with the speed lock on (or used this run) `speedLock` {`on`, `scale`, `step`, `max_step`, `limited`} and a `note` once it coarsened the reactive step |
| `sim skip [<time>\|step] [--count N] [--back]` | Pause and skip forward by a span (`5us`, `2.5ms`, `1min`), or by the skip interval (`settings skipInterval`) without one, as fast as the computer can whatever the set speed, then pause; replies after one frame with `from`, `skipped` and `note`. `step` = to the next solver event. `--count N`: N times as far (N solver steps). Starts a stopped simulation (a run starts at 1 ps, so `sim skip 1ms` from stopped lands on 1 ms + 1 ps, as `sim run --for`). The → key and the toolbar's ⏭ do the same (specs/sim-control.md). `--back`: go back as far with the keyframes of the run (`settings history`): the last keyframe at or before the target is restored, the rest is replayed, then it pauses; `skipped` is negative; ← and ⏮ do the same. A circuit with a part that can't be saved refuses, naming each part and why ("going back is off for this circuit: …"; `sim history` → `missing`) |
| `sim step [--count N] [--back]` | One solver step: run the next pending event time, solve and pause (`sim skip step`); `--back`: as many solver steps back, onto the earlier step times |
| `sim speed [percent]` | Target speed in % of real time |
| `noise sources` | The transient noise sources of the running circuit: part, type, kind (thermal / shot / channel), nodes, spectral density `psd` (A²/Hz) and the `rms` current per update. See [NOISE.md](NOISE.md) |
| `sim average --for <time> <pin\|comp> ...` | Run for a span, then pause, and give the time-weighted mean, RMS, min and max over every converged solver step: the voltage at a pin (`R1.lPin`), or for a two-pin part (`R1`, `C1`, `L1`) the voltage across it (pin 0 − pin 1) and the current into pin 0; a meter (`Amperimeter`, `Voltimeter`) the same between its two terminals (its current, the voltage across it). On a fresh run the unsolved start (before the first solved step) is kept out of min and max. Exact for switching waveforms; a mean of `scope data` samples is not, since they sit on a fixed time grid. A logic output with no wire (an unused Q̄) is read at every solved step; another pin with nothing connected is refused |

### Measuring

| Command | |
|---|---|
| `probe <pin> ...` | Voltage and current (into the component) at pins |
| `read <comp> ...` | Readouts: meters (value and unit, and `reading`: `Instant`, or `Mean` / `RMS` with `over`, the averaging block; set with the meter's properties `Reading` and `Avg_Time`, default 100 ms: the display shows the mean or r.m.s. of the last whole block of simulated time, with every solver step weighted by its duration, and is marked `avg` / `rms`), probes, frequency meters, LEDs (brightness, current, `overloaded`: `current_avg` above Max Current; `current_avg`: I through a 100 ms low-pass, compared with Max Current, IF; `current_peak`: the largest I of a solved step since the start, compared with Peak Current, IFP), resistors and varistors (voltage, current, power), diodes (voltage, current, power, and `current_avg`: \|I\| through a 100 ms low-pass, compared with Max Current, IF(AV); `current_peak`: the largest \|I\| of a solved step since the start, compared with Surge Current, IFSM), BJTs (terminal currents `i_base`, `i_collector`, `i_emitter` and the region `state`: OFF, ACTIVE or SAT), MOSFETs (`vgs`, `vds`, `i_drain` and the region `state`: OFF, ACTIVE or OHMIC), JFETs (the same plus `i_gate`; see `docs/JFET.md`), current source ICs (`voltage` V+ − V−, `v_r`, `i_set`, `i_r`, `i_bias`, `power` and `state`: REG, LOW V, OFF or a fault; see `docs/CURRENT_SOURCE_IC.md`), lamps (also resistance; with the filament model on, temperature in K and radiated W), capacitors (voltage, charge level, reference, flags), subcircuits with scripted cores (`state`: the text each script shows with `component.showValue`; with several cores `block: text | block: text`, e.g. `supply: ON | fb: OK | pwm: RUN`; and `status`: each block's state set with `component.setStatus`, with its level), plus pin voltages |
| `messages [--last N] [--grep TEXT]` | Text of the Messages pane, one line per entry (`lines`, `count`). Debug logs are only in it with `logVerbose` on |
| `log [--since T] [--kind part\|cap] [--comp C] [--last N]` | Event log of the run, as in Messages: part state changes and charge events of capacitors with Log charge on (time, approx, comp, kind, level, text, shown) |
| `status <comp> [--last N]` | States a part reports with `component.setStatus`: now, and every change since the start with its simulated time (block, text, level `off`/`on`/`wait`/`fault`), oldest first. Restart times and blink rates come straight from it |
| `scope data <comp> [--ch N] [--last TIME] [--max N]` | Oscilloscope or logic analyzer samples as `[time_s, value]`, oldest first; a value holds until the next sample. Each channel gives its `oldest` sample time; the oscilloscope gives `sampleSlot`, the resolution (one sample per time/div / 5000, the last change in it). Over `--max` (default 2000; 0 = all) the span is cut into `--max`/4 buckets, the same for every channel, each giving its first, lowest, highest and last sample at their own instants (peaks and edges kept), and the reply has `thinned` |
| `scope get <comp>` | Display settings: time/div, trigger, trigger level, auto-scale, and per channel volts/div and positions; for the oscilloscope also `sampleSlot` (time/div / 5000) and `historyMin` (BufferSize slots: at least that much history) |
| `scope set <comp> [--timediv T] [--trigger CH\|none] [--triglevel V\|auto] [--voltdiv V\|a,b,c,d] [--voltpos ...] [--timepos ...] [--ch CH] [--autoscale CH\|none]` | Change display settings, as in the Expand panel |

For `scope set`, channels are 0..3 (`Pin0`..`Pin3`). A single
`--voltdiv`/`--voltpos`/`--timepos` value applies to the channel given with
`--ch`, or to all channels; a comma list sets each channel. All values are
checked before anything changes. For example,
`scope set SCOPE --timediv 5ms --voltdiv 5 --trigger 0` shows a 50 Hz, 10 V
sine. The defaults (1 ms, 1 V/div) clip most signals.

`--triglevel` sets the oscilloscope's trigger level (Expand panel: Trig
level). `auto`, the default, triggers on a rising crossing of the middle of
the signal's range over the last 10 to 20 screens (hysteresis 15 % of that
range, holdoff one screen), with the crossing in the middle of the screen. A
burst, such as an LC ringing after each edge of a square wave, so shows its
first big cycles, not the flat gap between bursts. With no such crossing the
screen follows the measured period, or runs free. A
value in volts triggers on a rising edge through that level, with the Filter
setting as hysteresis: the crossing goes in the middle of the screen, drawn
as a dashed line with an arrow in the trigger channel's colour. With no
crossing in the last 10 screens the scope runs free, like a real scope in
Auto sweep. Example: `scope set SCOPE --trigger 0 --triglevel 300` on a
rectified mains bus.

`sim run` waits for one display frame after pausing, so `read` returns values
that match the end of the run. Oscilloscope and logic analyzer screens catch
up once when the simulation pauses, so a `capture` or `export` after a short
`sim run --for` (less than one 50 ms frame of wall time) shows the data up to
the pause.

### Interacting

| Command | |
|---|---|
| `ui controls <comp>` | Buttons, switches and dials of a part, with their state |
| `ui press <comp> [index]` / `ui release <comp> [index]` | Hold or release a push button (a press + release toggles a toggle switch, in the order of a mouse click) |
| `ui click <comp> [index]` | Click (press + release); toggles switches. `ui click <x,y>` (below) clicks the canvas instead |
| `ui set <comp> [index] on\|off` | Set a switch or toggle. Switch and DIP switch positions are saved with the circuit (Switch: hidden property `Toggled`, true when flipped away from its Normally Closed setting) |
| `ui dial <comp> [index] <value\|N%>` | Potentiometers, variable sources, encoders, sliders |
| `ui key <char> [down\|up\|tap]` | Keyboard key, for parts bound to a key |
| `ui type <key> [key ...] [--layout us\|de\|fr]` | Real key presses on the canvas, as typed: a character (`r`, `<`, `_`, `é`) or a name (`Left Right Up Down Tab Enter Esc Backspace Delete Space Home End Shift F1`…`F12`; `Shift` alone is pressed and released: it ends a selection rectangle, K11), modifiers joined with `+` (`shift+Left`, `cmd+z`). An uppercase letter implies Shift. The AZERTY number row without Shift is typed by its characters (`& é " ' ( è ç à`): each is its physical digit key with that text. With `--layout`, printable keys are sent as macOS sends them on a US QWERTY, German QWERTZ or French AZERTY keyboard (Shift where the layout needs it, the physical key code). A key bound to a toolbar button (`F5`, `cmd+,`) goes through Qt's shortcut map and triggers it, as a real key does. Gives the canvas focus first; returns `kbd state`. `--window` (last): the keys go to the widget that has the focus, or the window when none, and the canvas focus is left as it is: the simulation keys (Space, Esc, arrows while the canvas has no focus) |
| `ui toolbar` | The circuit toolbar's buttons: `text`, `tooltip` (with the key in this platform's notation: `⌘,` on macOS, `Ctrl+,` elsewhere), `shortcut` (the key bound to it, native text; empty when the canvas reads the key), `enabled`, `checked` (toggles) |
| `ui mouse <x,y>` | Move the mouse (no button) to a scene position over the canvas; returns `kbd state` |
| `ui drag <x1,y1> <x2,y2>` | Drag with the left mouse button between two scene positions (press, moves in 8 px steps, release); returns `kbd state` |
| `ui click <x,y> [--cmd] [--double]` | Left click at a scene position on the canvas, as the mouse does (on empty canvas: clears the selection, places the keyboard cursor; `--cmd` holds Cmd: on a wire, corner or dot it starts network mode there, and in network mode marks / unmarks a wire; `--double` adds a double click: with `--cmd` on a wire, its net is marked); returns `kbd state` |
| `ui click --widget messages\|toolbar` | Press and release on Messages or the simulation toolbar, as the mouse does: takes the keyboard focus from the canvas; returns `kbd state` |
| `ui focus canvas\|none` | Give the canvas keyboard focus or take it away |

These commands work on any part with on-screen controls, including push
buttons, switches, DIP switches, keypads, clocks, potentiometers, variable
sources, rotary encoders and joysticks.

**Contact bounce.** Switch and Push have `Bounce_Time` (0 = a clean change, the
default) and `Bounces` (5). With a Bounce_Time set, each change of the contact while
the simulation runs is a burst: the new state, then `Bounces` times back to the old
one and to the new one again, the gaps shrinking by 0.7 each time, all within
Bounce_Time (2·Bounces+1 changes), ending in the new state. The burst is the same
every time (no randomness). E.g. `comp set S1 Bounce_Time=2ms Bounces=4`, then
`ui click S1`: a debouncer (RC + Schmitt trigger, SR latch) sees what a real contact
gives it. The contacts at the start of a run are set cleanly.

### Keyboard input on the canvas

Keyboard-driven editing (`specs/keyboard-input.md`). Tests: `tests/kbd/run.sh`.

| Command | |
|---|---|
| `kbd partkey <part> [<letter>\|none] [--replace]` | A part's keyboard shortcut (the letter that places that kind of part at the cross), as its properties dialog sets it: `<part>` is a component on the canvas or a name in the component list. Without a letter: show it (`key`, `default`, `caption`). A letter another part has needs `--replace` (that part then has none); `h` `w` `k` and non-letters are refused; `none` clears. Saved, except on a headless server (in memory only) |
| `kbd state` | Focus, keyboard cursor (the cross, scene x,y on the grid lines at 4 + 8k), whether the cross is shown (focus and no part selected), the mouse pointer hidden, `panMode`, `zoom` (view scale), selected parts, the pins to wire (`pinFrom`, `pinTo`); with one part selected its `selectedPin` (`selectedPinManual`: chosen with `<` `>`, else auto) and `pinsClockwise`; while a value is typed `valueEdit` (part, property, buffer); in network mode (`=`) `network`: the `pin` walked from, the `walk` (its steps in order: `wire <uid>`, `corner <uid>#<point index>`, `dot <uid>`), the current `step` (kind, uid, pos, path length), the `selectedWires` / `selectedDots` highlighted and the `marked` wires (specs/wire-colors.md); while a wire is drawn from a wire (`+`) `drawing`: its `fixed` points, the `wire` uid and its current `points` |
| `kbd shortcuts [--table]` | Every shortcut of the circuit editor: category, action, keys (macOS words: `Cmd+Shift+Z`), context. `--table`: the rows of Settings > Shortcuts (K10): keys as this platform writes them (`⇧⌘Z` on macOS, `Ctrl+Shift+Z` elsewhere), plus the part letters in effect (category `Part letters`), in category order |

Headless, the window is never active, so `ui type` and `ui focus` set the canvas focus
of the keyboard state directly. The cross is drawn on the view only: `capture window view`
shows it, `capture circuit` and image exports don't.

### Seeing and pointing (working together)

To work on a circuit together, run the GUI (`simulide`) and drive the same
window from the CLI. You see every change as it happens, and the CLI side can
look at the window and point at parts.

| Command | |
|---|---|
| `capture circuit [comp ...] [--area x,y,w,h] [--margin PX] [--scale S] [--file F.png]` | Render the circuit, or just some parts, to a PNG |
| `capture window [main\|view\|circuit\|editor\|settings\|library\|props <comp>\|dialog <title>] [--scale S] [--file F.png]` | Grab what the window shows, or an open dialog |
| `export [--file F.png\|F.svg] [--format png\|svg] [--theme current\|Classic\|Light\|Dark] [--meters show\|hide] [--grid show\|hide] [--scale S] [--margin PX] [--no-embed-fonts]` | Export the whole circuit as a PNG or SVG image (the toolbar's Export button) |
| `select` | List the selected components |
| `select <comp ...> [--add]` / `select --none` | Select components (highlighted in the GUI), or clear the selection |
| `view fit\|selected\|reset` | Zoom to everything, to the selection, or to 100% |
| `view zoom <factor>` / `view center <comp\|x,y>` | Zoom by a factor, or center on a part or point |
| `view show <comp ...>` | Select the parts and zoom to them |
| `view raise` | Bring the window to the front |
| `view settings [app\|circuit\|simulation\|shortcuts\|close]` | Open the Settings dialog on a tab (`shortcuts`: the keyboard shortcut list, K10), or close it |
| `view props <comp> [--tab N\|NAME] [--close]` | Open a component's property dialog (on a tab, by number or name), or close it |
| `view export` | Show the Export dialog without blocking (the toolbar's Export button opens it modally) |

Capture details:

- **`capture circuit`** renders the scene itself, so it also works headless and
  doesn't depend on the window's zoom or scroll position.
  - `--scale` defaults to 2, and the longest side is capped at 4000 px.
  - The image shows live state: LEDs, switch positions, meter displays, scope
    screens and the selection.
- **`capture window`** captures:
  - `main`: the whole window (the default),
  - `view`: the circuit as you currently see it,
  - `circuit`: the circuit panel with its toolbar,
  - `editor`: the code editor,
  - `settings`: the Settings dialog (`view settings` opens it),
  - `library`: the component list (`lib select <name>` selects a category or component in it),
  - `props <comp>`: that component's property dialog (`view props` opens it),
  - `dialog <title>`: any open dialog whose title contains the text, e.g.
    `capture window dialog Export` after `view export`. The error lists the
    open dialogs when none matches.
  - `--scale 1` (the default) gives logical pixels, which is half size on
    HiDPI screens.
- **`export`** is the image export of the toolbar button (the picture icon
  after Save As, also in the canvas right-click menu), for documentation
  rather than for looking:
  - The format comes from `--format` or the file extension; PNG by default.
    SVG is vector (1 circuit unit = 1 CSS px).
  - `--theme` draws with a colour preset (Classic, Light, Dark) for this image
    only; the setting is not changed. Default: the current colours.
  - `--meters show|hide`: scopes, logic analyzers, voltmeters, frequency meters
    and probes with their leads, lead tunnels and the grounds only they use
    (as the Instruments toolbar button). Ammeters stay: they sit in series.
    Default: as the canvas shows them now.
  - `--grid show|hide`: default hide.
  - SVG embeds the bundled fonts its text uses (Inter, JetBrains Mono, as
    WOFF2 `@font-face` rules; about 30 to 60 KB each), so any browser shows
    the text as SimulIDE laid it out; `fonts` in the reply lists the faces.
    `--no-embed-fonts` leaves them out; `font-family` still lists fallbacks.
    Fonts that are not bundled (a Text component with another font) are not
    embedded: `circuit fonts` converts those.
  - The selection is not drawn (it is restored afterwards). The image covers
    everything that is drawn, plus `--margin` (default 24).
  - `--scale` (PNG only): pixels per circuit unit, default 2; the longest side
    is capped at 16000 px.
  - The reply gives the file, size, scene area and the options used.
- Without `--file`, images go to `$TMPDIR/simulide-capture/`. The reply gives
  the path.
- To show the other side what you mean, select parts in the GUI and they run
  `select`, or they run `view show R1 D1` and you see the parts highlighted.

### Microcontrollers

`<comp>` can be an MCU or a board that contains one, such as an Arduino.

| Command | |
|---|---|
| `mcu list` | MCUs in the circuit |
| `mcu info <comp>` | Device, firmware, clock, memory sizes, UART count |
| `mcu load <comp> <firmware>` | Load a `.hex` or `.elf` file |
| `mcu reload <comp>` | Reload the same file (after recompiling) |
| `mcu reset <comp>` | Reset the CPU; the simulation keeps running |
| `mcu main <comp>` | Make it the main MCU (used by the editor and debugger) |
| `mcu regs <comp> [--filter TEXT]` | Registers with their address and value |
| `mcu mem <comp> <addr\|reg> [count] [--space ram\|flash\|eeprom]` | Read memory |
| `mcu mem <comp> <addr\|reg> --set v,v,... [--space ...]` | Write memory |
| `mcu eeprom <comp> [--load F \| --save F \| --set v,v,...]` | EEPROM contents. `.data` files hold comma-separated values; `.hex` and `.bin` are also accepted |

### Serial (UART)

| Command | |
|---|---|
| `serial send <comp> [--uart N] [--hex] [--cr\|--lf\|--crlf] <text...>` | Send bytes |
| `serial read <comp> [--uart N] [--wait T] [--until TEXT] [--timeout T] [--peek] [--hex] [--rx\|--tx]` | Bytes captured since the last read |
| `serial clear <comp> [--uart N]` | Discard captured bytes |

Direction depends on the part:

- **MCU or board:** `send` delivers bytes to its RX, as if a host sent them, and
  `read` returns what it transmitted.
- **Serial Terminal or Serial Port:** these are the host side. `send` transmits
  into the circuit, and `read` returns what they received. `--rx` and `--tx`
  override the direction.

Timing:

- Sent bytes are delivered one frame at a time at the UART's baud rate, starting
  at the next display frame. If the firmware hasn't enabled its receiver yet,
  they wait in a queue.
- Captured bytes are kept even if no serial monitor window is open, and are
  cleared when the simulation starts.
- `--wait` and `--until` wait in real time while the simulation runs, for
  example with `sim resume`. For deterministic scripts, use `sim run --for`
  followed by a plain `serial read`.

### I2C

| Command | |
|---|---|
| `i2c <master> <addr> [--write b,b,...] [--read N] [--timeout T]` | One transfer through an **I2C Master** part (`comp add I2CMaster`, in Logic/Converters) wired to the bus: START, address+W, the `--write` bytes, repeated START, address+R, `--read N` bytes (ACK on each, NACK on the last), STOP |

The I2C Master is a test part: it plays the firmware of an MCU's TWI peripheral, so slave parts
(DS1621, DS1307, I2C RAM, I2C to Parallel, SSD1306, ...) can be checked from a script. Its two
open-collector pins, SDA and SCL, have internal 100 kΩ pull-ups (`Pullups`, on by default);
`Frequency` sets the clock (100 kHz).

- The address is 7-bit, without the R/W bit: `0x48` for a DS1621 with A0–A2 low.
- Bytes take `42`, `0x2A` or `0b101010`.
- It starts or resumes the simulation and leaves it running; `--timeout` is simulated time
  (default 100 ms).
- The reply gives `read` (bytes), `write_ack` (one per written byte), `addr_ack_w` /
  `addr_ack_r`, `ok`, an `error` when a byte was not acknowledged (the master then sends STOP),
  and `log`, the bus in order: `S A0x90+ W0xAA+ Sr A0x91+ R0x19+ R0x00- P` (`+` ACK, `-` NACK).

```sh
simulide cli comp add I2CMaster --label M
simulide cli i2c M 0x48 --write 0xEE                 # DS1621: Start Convert
simulide cli sim run --for 1s                        # 750 ms conversion
simulide cli i2c M 0x48 --write 0xAA --read 2        # Read Temperature: [25, 0] = 25.0 °C
```

### Code editor, compiler and debugger

| Command | |
|---|---|
| `editor open <file>` | Open a source file (or make it current) |
| `editor files` / `editor status` | Open files, current file, compiler, debugger state and line |
| `editor save [file]` / `editor reload [file]` / `editor close [file] [--discard]` | |
| `editor compilers` | Available compilers and assemblers |
| `editor compiler [name] [setting=value ...]` | Show or set the file's compiler and its settings (tool path, include path, device, ...) |
| `editor compile [file]` | Compile. Returns the compiler output, or an error that includes it |
| `editor upload [file]` | Compile and load the firmware into the main MCU |
| `editor debug start\|step\|over\|run\|pause\|reset\|stop\|status [--wait T]` | Source-level debugger |
| `editor break add\|remove\|list\|clear [line ...]` | Breakpoints of the current (or `--file`) file |

- `step` and `over` reply when the next source line is reached.
- `run --wait T` replies when a breakpoint is hit, or after `T`.
- Compiling needs the toolchain installed (avr-gcc, Arduino, gputils, SDCC, ...)
  and configured, as in the GUI. Scripted (AngelScript) devices compile with
  no external tools; their compile errors (file, line, message) appear in
  Messages and in the reply of the command that loaded them.

### Settings

| Command | |
|---|---|
| `settings list [--group app\|circuit\|simulation]` | All settings with their values and descriptions |
| `settings get <name>` | One value |
| `settings set <name> <value>` or `settings set a=1 b=2` | Change settings |

The groups follow the Settings dialog:

| Group | Settings |
|---|---|
| app | `language`, `fontScale`, `fontName`, `userPath` |
| circuit | `drawGrid`, `showScroll`, `animate`, `fps`, `autoBackup` |
| simulation | `speed` (also sets `skipInterval` to a tenth of a second at that speed, `1 step` kept), `skipInterval`, `history` (keyframes every frame, on by default), `historyBudget` (MB, default 256), `runSeed` (rand() seed at each start, 0 = new), `speedLock` (hold the set speed by coarsening the reactive step, specs/sim-control.md §3.9), `speedLockMaxStep` (its limit, 0 = 100 × `reactStep`), `stepSize`, `stepsPerSec` (read only), `reactStep`, `nlSteps`, `slopeSteps`, `supplyRamp`, `noise`, `noiseBW`, `noiseScale`, `noiseSeed`, `noiseStart`, `noiseStop`, `noiseOff` |

- Simulation settings are saved with the circuit.
- `logVerbose` (Messages pane: Log: Verbose) also shows debug logs, the lines
  that start with a `[tag]`. Off (the default), Messages shows only information
  (upstream's messages, `qInfo`), warnings and errors; debug logs always go to
  stderr. Saved in the user settings.
- `stepSize` keeps the step you give. The simulator runs at most 1e9 steps per
  second, so when the current speed would need more, the speed is lowered and
  the reply row gets a `note`, e.g. "speed lowered from 100 % to 10 % so a 1 ns
  step stays under 1e9 steps per second". `speed` (and `sim speed`) keeps the
  speed and changes the step instead, also with a `note`.
- `language` and `userPath` are saved to the user settings immediately.
- Font changes take effect after a restart.
- `supplyRamp`: DC sources (Battery, Rail, Fixed Voltage) rise linearly from 0 to their
  value over this time at each fresh start (50 steps; `0`, the default, is a step). A loop
  started by an instant step can settle in a state a real, ramping supply never visits (an
  op-amp current sink railed, a JFET cascode dead at some loads). The value is saved in the
  circuit; a voltage changed during a run applies at once. An op amp with `Power_Pins=false`
  keeps its internal rails.
- `noise*`: transient noise of the parts, off by default ([NOISE.md](NOISE.md)). They
  apply at the next start (a change while running gets a `note`). `sim status` always
  has a `noise` field: `"off"`, or the settings, the `state` (armed / on / ended /
  stopped), `active` and the `seed` in use; `sim average` and `scope data` add the
  same object when noise was active during their span (for `scope data`: from its
  oldest sample returned to now). `probe` and `read` add it to each entry once noise
  has been active in the run, also after the window: an instant value then still
  follows from the noise drawn (a bistable's state). A run without noise has no
  `noise` field anywhere.

### Reactive accuracy (inductors, capacitors and transformers)

These components use backward-Euler companion models. The method is stable and
introduces numerical damping; small phase or per-step errors can noticeably lower
the gain of a high-Q resonant circuit. This damping is not a physical winding
resistance. Lower **`reactStep`**, not just `stepSize`, to improve accuracy.

Stop the simulation before changing the circuit-wide reactive step so that all
companions are stamped with the new interval on the next start:

```sh
simulide cli sim stop
simulide cli settings set reactStep=10ns
simulide cli sim run --for 40ms
```

Inductors and capacitors also have a per-part `ReaStep` override (0 means use the
circuit setting). Reset any overrides to 0 if you want a circuit-wide change to
apply to every part. Transformers use the circuit-wide setting. Smaller steps
cost more simulation work, so use them where accuracy requires it and verify
convergence by reducing the step again.

For example, a series 50 mH / 120 nF / 54 ohm bandpass driven at 2055 Hz has an
ideal resistor/input gain of 0.999993. Backward Euler predicts approximately
0.86629 at 1 us, 0.98479 at 100 ns, and 0.99845 at 10 ns. Near resonance the
pair's numerical series resistance is approximately `reactStep / C`; for a
target fractional gain error `epsilon`, start with
`reactStep <= epsilon * R * C / (1 - epsilon)` and check convergence. A transformer
winding also has the small physical resistance specified by its model.

Use **one** `sim average` call for input and output, over an integer number of
periods. Separate RMS windows that contain fractional cycles can bias the gain
ratio. Runnable examples: `tests/circuits/reactive_bandpass.sim1` and
`tests/circuits/transformer_bandpass.sim1` (both saved with a 10 ns reactive step).

## Examples

**Build, run and measure:**

```sh
simulide cli - <<'EOF'
circuit new --discard
comp add Battery  --label B1 --at 0,0     Voltage=9
comp add Push     --label SW --at 40,-40
comp add Resistor --label R1 --at 120,-40 Resistance=470
comp add Led      --label D1 --at 200,0   --rot 90
comp add Ground   --label G  --at 0,80
wire add B1.lPin SW.pinP0
wire add SW.switch0pinN R1.lPin
wire add R1.rPin D1.lPin
wire add D1.rPin G.Gnd
wire add B1.rPin G.Gnd
ui press SW
sim run --for 20ms
read D1 R1
EOF
```

**Arduino serial echo:**

```sh
cd resources/examples/Micro/Arduino/arduino_serial_echo
simulide cli circuit open arduino_serial_echo.sim1
simulide cli mcu load Uno-1 arduino_serial_echo.hex
simulide cli sim run --for 200ms
simulide cli serial read Uno-1          # Serial echo Test
simulide cli serial send Uno-1 hello --lf
simulide cli sim run --for 50ms
simulide cli serial read Uno-1          # hello
```

## JSON protocol

The socket speaks newline-delimited JSON, one object per line, so any language
can drive SimulIDE without the `simulide cli` client. Use `QLocalSocket`, or a
Unix socket client on the path that Qt uses for the name. On macOS and Linux
that path is `$TMPDIR/<name>` or `/tmp/<name>`.

A request has one of two forms:

```json
{"id": 1, "argv": ["comp", "add", "Resistor", "--at", "100,100", "Resistance=1k"], "cwd": "/home/me/project"}
{"id": 2, "cmd": "comp add", "args": {"_": ["Resistor"], "at": "100,100", "set": {"Resistance": "1k"}}}
```

- **`argv`:** the same words as on the command line.
- **`cmd` + `args`:** the structured form.
  - `_` holds the positional arguments.
  - `set` holds the `name=value` pairs.
  - Every other key is an option.
- **`cwd`** (optional) resolves relative file paths.

The reply has the same `id`:

```json
{"id": 1, "ok": true,  "result": {"uid": "Resistor-1", "...": "..."}}
{"id": 2, "ok": false, "error": "unknown property 'Bogus' for Resistor-1 ...", "messages": ["..."]}
```

- `messages` is present when dialogs were closed or warnings were raised while
  the command ran.
- Commands run one at a time on the GUI thread, in the order they arrive on a
  connection.
- Long-running commands (`sim run`, `serial read --wait`, `editor debug step`)
  reply when they finish, and the connection waits until then.

Python example:

```python
import json, os, socket, tempfile

path = os.path.join(tempfile.gettempdir(), "simulide-cli")   # Qt's local socket path
s = socket.socket(socket.AF_UNIX); s.connect(path)
f = s.makefile("rw")

def cli(*argv):
    f.write(json.dumps({"id": 1, "argv": list(argv), "cwd": os.getcwd()}) + "\n"); f.flush()
    r = json.loads(f.readline())
    if not r["ok"]: raise RuntimeError(r["error"])
    return r["result"]

cli("circuit", "new", "--discard")
print(cli("comp", "add", "Resistor", "Resistance=1k"))
```

## Testing

`tests/cli/smoke.sh [path/to/simulide]` starts a headless instance on a private
socket and checks the main features end to end. It needs `python3`.
