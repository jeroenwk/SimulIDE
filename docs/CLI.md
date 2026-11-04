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
simulide --headless [--socket NAME] [file]    No window; exits on "quit"
simulide --no-server                          GUI without the CLI socket
```

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
| `undo`, `redo` | Same as Ctrl+Z / Ctrl+Y |
| `quit [--discard]` | Close SimulIDE |

Commands that would throw away unsaved changes refuse unless you pass `--discard`.

### Library and components

| Command | |
|---|---|
| `lib list [--search TEXT] [--category CAT]` | Everything that can be added: built-in parts, MCUs, ICs, boards |
| `comp add <name> [--at x,y] [--label L] [--rot DEG] [prop=value ...]` | Add a part. Prints its uid and where each pin is (`pos` x,y and the `side` it points to) |
| `comp list [--type TYPE]` | Components in the circuit |
| `comp get <comp> [--all]` | Properties (with units, or the allowed options for choice properties) and pins (with position, connections, and voltages while running). `--all` adds hidden properties, marked `hidden` |
| `comp set <comp> prop=value ...` | Change properties. `label=` renames. Undoable |
| `comp move <comp> <x,y>` | Move (snapped to the grid) |
| `comp rotate <comp> [cw\|ccw\|180]` | Rotate |
| `comp flip <comp> <h\|v>` | Flip |
| `comp remove <comp> ...` | Remove parts and their wires |
| `comp link <comp> [target ...] [--clear]` | Links of a linker part (scripted core, MCU): the parts it drives, in order (index 0, 1, ... of `component.setLinkedValue` in a script). Targets are appended, `--clear` removes all first, no target lists them. Like the GUI's Link mode, not undoable |

- `comp add` is atomic: if a property is invalid, nothing is added.
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
| `wire add <pinA\|@x,y> <pinB\|@x,y> [--via x,y,...]` | Connect two pins, or a pin and a point on a wire (a new junction there). The route goes through the `--via` points and is always horizontal/vertical |
| `wire list` | Wires and the pins they join |
| `wire remove <wire> ...` | Remove wires. Prints the removed uids and any merged wires |
| `wire current [wire ...]` | Current through wires (A), signed from the `from` pin to the `to` pin. Works paused. `flag` marks shared (`approximate`) or non-conserving (`inconsistent`) results |

- `comp move`, `comp rotate` and `comp flip` also print the new pin positions.

Connecting to a pin that already has a wire inserts a junction node on that
wire, as clicking on a wire does in the GUI. The junction goes in the middle of
the wire segment next to that pin, on the 8 px grid. To put it somewhere
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
  **SAT** (amber, saturated: VCE < 0.2 V); MOSFETs **OFF** / **ACTIVE** /
  **OHMIC**; SCRs, triacs and diacs **OFF** or **ON** (amber, conducting); op
  amps **LINEAR** / **SAT+** / **SAT-** (amber, at a rail); voltage regulators
  **REGULATING** / **DROPOUT** (amber) / **OFF**; diodes **OFF** / **ON** /
  **BREAKDOWN** / **OVER**, zeners **REGULATING** in breakdown, LEDs **OFF** /
  **ON** / **OVER**. Diodes and LEDs have Show State off by default (there are
  usually many): turn it on for the ones to watch. Each boundary has a little hysteresis. The state
  is taken when each time step has converged, so the times are exact at any
  speed. `status` lists them, `read` gives the current one as `state`, and
  Messages logs them. A part's **Show State** property (`Show_State`) turns its
  badge and log off.
- MOSFETs (also depletion mode, used for JFETs) report **OFF** (grey, VGS below
  turn-on or past pinch-off), **ACTIVE** (green, amplifying: the FET's
  "saturation" region, VDS > VGS − Vth) and **OHMIC** (amber, fully on, a
  resistor: VDS < VGS − Vth), the same way.

**Event log (Messages):** while the simulation runs, the Messages pane lists
what happens, with the simulated time, coloured like the part-state badges:
```
[   312.009 ms] IC1  supply: UVLO → ON  ·  pwm: IDLE → WAIT COMP
[   414.817 ms] IC1  fb: OK → OVP  ·  supply: ON → HICCUP  ·  pwm: RUN → STOP → IDLE
[≈  419.683 ms] C5  charged to 23.7 V
[     1.4017 s] IC1  repeats the same cycle of 5 changes every 340.234 ms (2.94 Hz): not listed further while it repeats
```
- Part states: every `setStatus` change, exact time; changes within 1 µs share a line.
- Capacitors whose **Log charge** property is on (`comp set C5 LogCharge=true`):
  charging, charged, discharging, discharged, above / back within the rated
  voltage. Seen once per frame from 20 ms averages (mains ripple averages
  out), with a 3 % hysteresis, so the time is marked ≈.
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

A capacitor's `MaxVolt` property (rated voltage) makes its gauge red and
blinking above it, and draws a tick at that level. `read C1` gives the
numbers: `voltage`, `level` (can exceed 1), `vref`, `vref_source`
(`override`, `sources` or `peak`), `rating` and `flag` (`overvoltage`,
`reversed`, `overflow`).

**Colors:** `settings set colorTheme Classic|Light|Dark` picks a preset (Dark
is inspired by EveryCircuit: black canvas, hollow light grey parts, pale green
current dots); it reads `Custom` once a color was changed.
`settings set canvasColor|wireColor|lineColor|textColor|bodyColor|flowColor|nameColor|valueColor <color>`
sets the canvas, wires, component outlines (lines, pins, arrowheads), text
inside components (pin names), component bodies (parts drawn white by default),
current flow dots, and the name (R1) and value (470 Ω) labels. Animate colors:
`animHighColor`/`animLowColor` (wires above/below 2.5 V), `outHighColor`/`outLowColor`
(output pins), `inHighColor`/`inLowColor` (input pins) and `openHighColor`
(open-drain output released). A color is `#rrggbb` or a name (`white`, `navy`); `default` restores
the classic one. All of it is in Settings > Circuit > Colors too, with a reset
button. Animate colors wires by voltage (red/blue) and overrides the wire color.
`capture circuit` uses the canvas color.

Routing:
- Where two consecutive points differ in both x and y, `wire add` adds a corner.
  The wire leaves its first pin in the direction the pin points, then turns at
  each corner.
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
| `sim status` | State, simulated time, target and actual speed |
| `sim speed [percent]` | Target speed in % of real time |

### Measuring

| Command | |
|---|---|
| `probe <pin> ...` | Voltage and current (into the component) at pins |
| `read <comp> ...` | Readouts: meters (value and unit), probes, frequency meters, LEDs (brightness, current), resistors and varistors (voltage, current, power), BJTs (terminal currents `i_base`, `i_collector`, `i_emitter` and the region `state`: OFF, ACTIVE or SAT), MOSFETs (`vgs`, `vds`, `i_drain` and the region `state`: OFF, ACTIVE or OHMIC), lamps (also resistance; with the filament model on, temperature in K and radiated W), capacitors (voltage, charge level, reference, flags), subcircuits with scripted cores (`state`: the text each script shows with `component.showValue`; with several cores `block: text | block: text`, e.g. `supply: ON | fb: OK | pwm: RUN`; and `status`: each block's state set with `component.setStatus`, with its level), plus pin voltages |
| `log [--since T] [--kind part\|cap] [--comp C] [--last N]` | Event log of the run, as in Messages: part state changes and charge events of capacitors with Log charge on (time, approx, comp, kind, level, text, shown) |
| `status <comp> [--last N]` | States a part reports with `component.setStatus`: now, and every change since the start with its simulated time (block, text, level `off`/`on`/`wait`/`fault`), oldest first. Restart times and blink rates come straight from it |
| `scope data <comp> [--ch N] [--last TIME] [--max N]` | Oscilloscope or logic analyzer samples as `[time_s, value]`, oldest first |
| `scope get <comp>` | Display settings: time/div, trigger, auto-scale, and per channel volts/div and positions |
| `scope set <comp> [--timediv T] [--trigger CH\|none] [--voltdiv V\|a,b,c,d] [--voltpos ...] [--timepos ...] [--ch CH] [--autoscale CH\|none]` | Change display settings, as in the Expand panel |

For `scope set`, channels are 0..3 (`Pin0`..`Pin3`). A single
`--voltdiv`/`--voltpos`/`--timepos` value applies to the channel given with
`--ch`, or to all channels; a comma list sets each channel. All values are
checked before anything changes. For example,
`scope set SCOPE --timediv 5ms --voltdiv 5 --trigger 0` shows a 50 Hz, 10 V
sine. The defaults (1 ms, 1 V/div) clip most signals.

`sim run` waits for one display frame after pausing, so `read` returns values
that match the end of the run.

### Interacting

| Command | |
|---|---|
| `ui controls <comp>` | Buttons, switches and dials of a part, with their state |
| `ui press <comp> [index]` / `ui release <comp> [index]` | Hold or release a push button |
| `ui click <comp> [index]` | Click (press and release); toggles switches |
| `ui set <comp> [index] on\|off` | Set a switch or toggle |
| `ui dial <comp> [index] <value\|N%>` | Potentiometers, variable sources, encoders, sliders |
| `ui key <char> [down\|up\|tap]` | Keyboard key, for parts bound to a key |

These commands work on any part with on-screen controls, including push
buttons, switches, DIP switches, keypads, clocks, potentiometers, variable
sources, rotary encoders and joysticks.

### Seeing and pointing (working together)

To work on a circuit together, run the GUI (`simulide`) and drive the same
window from the CLI. You see every change as it happens, and the CLI side can
look at the window and point at parts.

| Command | |
|---|---|
| `capture circuit [comp ...] [--area x,y,w,h] [--margin PX] [--scale S] [--file F.png]` | Render the circuit, or just some parts, to a PNG |
| `capture window [main\|view\|circuit\|editor] [--scale S] [--file F.png]` | Grab what the window shows |
| `select` | List the selected components |
| `select <comp ...> [--add]` / `select --none` | Select components (highlighted in the GUI), or clear the selection |
| `view fit\|selected\|reset` | Zoom to everything, to the selection, or to 100% |
| `view zoom <factor>` / `view center <comp\|x,y>` | Zoom by a factor, or center on a part or point |
| `view show <comp ...>` | Select the parts and zoom to them |
| `view raise` | Bring the window to the front |

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
  - `editor`: the code editor.
  - `--scale 1` (the default) gives logical pixels, which is half size on
    HiDPI screens.
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
| simulation | `speed`, `stepSize`, `stepsPerSec` (read only), `reactStep`, `nlSteps`, `slopeSteps` |

- Simulation settings are saved with the circuit.
- `language` and `userPath` are saved to the user settings immediately.
- Font changes take effect after a restart.

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
