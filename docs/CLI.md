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
| `circuit info` | File, modified flag, counts, simulation settings |
| `circuit export` | The circuit as `.sim1` XML |
| `undo`, `redo` | Same as Ctrl+Z / Ctrl+Y |
| `quit [--discard]` | Close SimulIDE |

Commands that would throw away unsaved changes refuse unless you pass `--discard`.

### Library and components

| Command | |
|---|---|
| `lib list [--search TEXT] [--category CAT]` | Everything that can be added: built-in parts, MCUs, ICs, boards |
| `comp add <name> [--at x,y] [--label L] [--rot DEG] [prop=value ...]` | Add a part. Prints its uid |
| `comp list [--type TYPE]` | Components in the circuit |
| `comp get <comp>` | Properties (with units) and pins (with connections, and voltages while running) |
| `comp set <comp> prop=value ...` | Change properties. `label=` renames. Undoable |
| `comp move <comp> <x,y>` | Move (snapped to the grid) |
| `comp rotate <comp> [cw\|ccw\|180]` | Rotate |
| `comp flip <comp> <h\|v>` | Flip |
| `comp remove <comp> ...` | Remove parts and their wires |

- `comp add` is atomic: if a property is invalid, nothing is added.
- Adding or removing parts stops a running simulation, as it does in the GUI.

### Wires

| Command | |
|---|---|
| `wire add <pinA> <pinB> [--via x,y,...]` | Connect two pins (L-shaped route, or through the `--via` points) |
| `wire list` | Wires and the pins they join |
| `wire remove <wire> ...` | Remove wires |

Connecting to a pin that already has a wire inserts a junction node on that
wire, as clicking on a wire does in the GUI.

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
| `probe <pin> ...` | Voltage at pins |
| `read <comp> ...` | Readouts: meters (value and unit), probes, frequency meters, LEDs (brightness, current), resistors (voltage, current, power), plus pin voltages |
| `scope data <comp> [--ch N] [--last TIME] [--max N]` | Oscilloscope or logic analyzer samples as `[time_s, value]`, oldest first |

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
  no external tools.

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
