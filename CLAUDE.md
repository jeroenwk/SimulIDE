# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

SimulIDE (v1.1.0-SR2) is a real-time electronic circuit simulator with microcontroller emulation (AVR/Arduino, PIC, 8051, 6502, Z80, scripted MCUs) and a basic code editor/debugger. It is a Qt5 C++11 desktop app. It aims for fast, simple models, not accurate circuit analysis.

## Build

Qt5 dependencies: Core, Gui, Xml, Widgets, Concurrent, Svg, Multimedia, SerialPort, plus qmake and `lrelease`.

```sh
cd build_XX
qmake        # build_XX/SimulIDE_Build.pro sets BUILD_DIR and includes ../SimulIDE.pro
make -j8
```

- Output goes to `build_XX/executables/SimulIDE_<version>/` (on macOS, `simulide.app` inside it). Objects and moc files go to `build_XX/build/`.
- Pre-build step: `lrelease` compiles `resources/translations/*.ts` to `.qm` files and moves them into `resources/qm`. Post-build step: copies `resources/data` and `resources/examples` next to the executable. The app reads component and MCU definitions from that copied `data/` folder at runtime, so you must rebuild (or recopy) after editing `resources/data`.
- **macOS (Apple Silicon)**: build with Apple clang against Homebrew `qt@5`, which is keg-only, so put it on PATH first: `export PATH=/opt/homebrew/opt/qt@5/bin:$PATH`. The binary is `build_XX/executables/SimulIDE_<ver>/simulide.app/Contents/MacOS/simulide`, and you can pass it a `.sim1` path to open a circuit.
- **AngelScript on arm64**: the bundled AngelScript (`src/angel`, v2.35.1, trimmed) has had native arm64 calling backported from 2.38:
  - `as_callfunc_arm64.cpp` and `as_callfunc_arm64_xcode.S` were added; the `.pro` adds the `.S` only on macOS arm64.
  - The Apple `__aarch64__` block in `as_config.h` was replaced.
  - The `baseOffset>>1` fix was applied in `as_callfunc.cpp`; without it, virtual methods crash.
  - The x86-only JIT (`src/angel/JIT`) is excluded on non-x86 via `SIMULIDE_NO_JIT`. Code that calls scripts must then use the plain call (`callFunction0`), not `executeJit0`, which jumps to the missing JIT code (`scriptcpu.cpp` guards both `SIMULIDE_W32` and `SIMULIDE_NO_JIT`).
  - Known limitation: upstream's ARM64 code can't take a POD struct larger than 16 bytes **by value**. Use references or ref types in script APIs.
- Source files are picked up by glob (`$$files(src/*.cpp, true)`), so new `.cpp`/`.h`/`.ui` files need no `.pro` edit, but **rerun `qmake`** so the Makefile picks them up (otherwise you get undefined-symbol link errors). A **new subdirectory** must be added to `INCLUDEPATH` in `SimulIDE.pro`, because includes are written flat (`#include "resistor.h"`), without a path.
- **macOS bundle `Info.plist`** comes from `resources/macos/Info.plist` (`QMAKE_INFO_PLIST`; it declares the `.sim1` / `.simu` document types). The Makefile only writes it when the bundle has none, so after editing the template delete `simulide.app/Contents/Info.plist` and rerun `make`, then `lsregister -f` the app (`/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister`). Finder opens a document with a `QEvent::FileOpen` (handled in `main.cpp`), not through `argv`.
- Release flags include `-Ofast`. Debug builds use `-O0 -D_GLIBCXX_ASSERTIONS`.

The repo has no linter or CI. The only automated test is `tests/cli/smoke.sh` (needs `python3`). It starts a headless instance and drives it through the CLI: it builds a circuit, measures it, and runs the Arduino serial and scripted SPI examples. Otherwise, verify a change by building and running the app, for example with circuits from `resources/examples`. The CLI is also the easiest way to check behaviour without clicking through the GUI (see below).

Circuits built while developing or checking a feature are saved in `tests/circuits/` (listed with what to expect in its README), so they can be opened in the GUI.

## Architecture

### Two layers: GUI component and simulation element
- `src/gui/circuitwidget/` holds the Qt graphics side: `Circuit` (the QGraphicsScene; it saves and loads circuits), `Component`/`CompBase` (the base for every placeable item, with properties), `Pin`, `Connector`, `Node`.
- `src/simulator/` holds the engine: `Simulator`, `eElement`, `eNode`, `ePin`, `CircMatrix` (the MNA matrix solver: one LU per group of connected nodes, no pivoting; the nodes are put in minimum-degree order and only the structurally nonzero entries, fill-in included, are factorized, which makes switching circuits several times faster than the original dense LU; `circuit info` shows the group sizes and factorization counts). The sparsity pattern comes from the node connections (`addConnection`, `addSingAdm`) made in `stamp()`, before `createMatrix()`: an element must not add a matrix entry between nodes it did not connect there. Basic electrical elements (`eResistor`, `eDiode`, etc.) live in `src/simulator/elements/`.
- A concrete component usually inherits from both layers, e.g. `class Resistor : public Comp2Pin, public eResistor`, and maps its GUI pins onto engine pins (`m_ePin[i] = m_pin[i]`).

### Simulation loop (`src/simulator/simulator.cpp`)
- This is an event-driven solver. On each GUI timer tick, `runCircuit()` runs in a worker thread (`QtConcurrent::run`) up to the next frame time (all times are in **picoseconds**, `uint64_t`).
- Elements hook in by overriding the `eElement` virtuals:
  - `initialize()`: reset state when the simulation starts.
  - `stamp()`: write admittances and currents into the matrix.
  - `runEvent()`: callback for an event scheduled with `Simulator::self()->addEvent(delay_ps, this)`.
  - `voltChanged()`: called when a connected node's voltage changes. Non-linear elements are iterated until they converge (limited by `maxNlSteps`).
- `solveCircuit()` re-solves the matrix only when nodes have changed, then processes the non-linear list and the voltage-changed list.
- Matrix conditioning: keep admittances within ~1e15 of each other. A floating `WaveGen` used 1e9 S between its pins; with only off diodes (~1e-9 S leakage) to the rest of the circuit, the pair's potential was lost in rounding and the diodes never converged (bridge rectifier on mains). It now uses 1 mΩ.
- Non-linear iterations are limited per step (`maxNlSteps`, default 100000). A step that reaches the limit is taken unconverged and reported (`takeNlReports()`: time, iterations, elements still iterating; the EventLog writes it in red to Messages). The following steps get at most 1000 iterations until one converges, then the full budget again.
- GUI-rate visual updates go through `Updatable::updateStep()` (called from the main thread's `timerEvent`), not from simulation callbacks.
- Most core managers are singletons reached through `X::self()` (`Simulator`, `Circuit`, `CircuitWidget`, `ItemLibrary`, `EditorWindow`, `InfoWidget`, ...).

### Adding a native component
1. Create the class under the right `src/gui/circuitwidget/components/<category>/` folder.
2. Give it a static `construct(type, id)` and a static `libraryItem()` that returns a `LibraryItem(name, category, icon, type, construct)`.
3. Declare properties in the constructor with `addPropGroup({...})`, using the typed property templates in `properties/` (`DoubProp`, `IntProp`, `BoolProp`, `StrProp`, ...) that bind getter/setter member pointers.
4. Register it in `ItemLibrary::loadItems()` (`src/gui/circuitwidget/itemlibrary.cpp`), and add the `#include` there.
5. Wrap translatable strings with `#define tr(str) simulideTr("ClassName",str)`.

### Data-driven components (`resources/data/`)
- `ComponentSelector` loads every `resources/data/*.xml` (e.g. `avr.xml`, `pics.xml`, `ic74.xml`, `arduinos.xml`). Each `<item>` points to a `package` (pin layout/graphics) and a `data` file.
- **MCUs**: `McuCreator::createMcu()` (`src/microsim/mcucreator.cpp`) parses the MCU's data XML. The root `core` attribute selects the CPU (`AVR`, `Pic12`, `Pic14`, `Pic14e`, `8051`, `6502`, `Z80`, `Z80ULA`, `scripted`). The XML elements then instantiate generic peripheral modules (ports, timers, UART, SPI, TWI, ADC, EEPROM, interrupts...) from `src/microsim/` and their core-specific subclasses from `src/microsim/cores/<arch>/`. Register writes reach peripherals through `watchRegister` callbacks on the `McuDataSpace`.
- **Subcircuits** (`src/gui/circuitwidget/subcircuits/`): ICs, boards, shields and modules are defined as circuit files plus a package and need no C++. The subcircuit type (`Logic`, `Board`, `Shield`, `Module`) picks the subclass.
- **Scripted devices**: `src/angel/` is a vendored AngelScript engine (it is the reason for several `-Wno-*` flags; don't modify it). `src/microsim/cores/scripted/` exposes CPU, ports, peripherals and displays to scripts, which live in `resources/data/scripted/`.

### Working on circuits together with the user
When the user wants to build or try circuits together, follow **`docs/COLLABORATION.md`**. The user starts a session with `/collab`, a project skill in `.claude/skills/collab/` whose `start.sh` launches or reuses the GUI and prints the circuit state. In short:
- Launch, or reuse, the visible GUI.
- Drive it with `simulide cli`.
- Check every step with `capture circuit` plus reading the PNG.
- Leave the simulation running.
- Save to `samples/`.

### Command line interface (`src/cli/`, documented in `docs/CLI.md`)
- **Entry points:**
  - `simulide cli <cmd>` is a thin client (`cliclient.cpp`, no GUI). `main.cpp` dispatches to it when `argv[1]=="cli"`.
  - `simulide --headless [--socket N]` sets `QT_QPA_PLATFORM=offscreen` and never shows the window.
  - The GUI also listens on the default `simulide-cli` socket.
- **Server** (`cliserver.cpp`):
  - A `QLocalServer` that speaks newline-delimited JSON. Requests are handled one at a time per connection, on the GUI thread.
  - In headless mode an app-wide event filter closes every `QDialog` as it is shown and returns its text as a reply `messages` note.
- **Commands:** registered with `CliApi::add(name, usage, help, handler)` in the `cli_*.cpp` files by area.
  - Handlers report errors with `throw CliError(...)`.
  - A long-running command calls `c.defer()` and later completes the returned `CliPending`, usually from `Cli::pollUntil`.
  - `CliRefs` (`clirefs.cpp`) resolves component, pin and wire references.
- **Threading:**
  - The simulation worker runs between GUI timer ticks, so CLI handlers can run while it is busy.
  - Anything that touches the event queue or MCU state must run from an `Updatable::updateStep()`, which is called while the worker is idle. `cli_mcu.cpp` does this through `CliTicker::runIdle`.
  - UART bytes are injected by an `eElement` that schedules simulation events, one frame each (`UartFeeder`).
- **Lifetime:**
  - `Simulator::self()` changes on every `circuit new` or `circuit open`, so re-register Updatables with the current simulator, as `frameCounter()` and `ticker()` do.
  - Components are not QObjects. Check that they still exist through `Circuit::compList()`, not with `QPointer`.
- **Seeing the circuit:** `capture circuit` renders the scene to a PNG (it works headless and doesn't depend on the window's zoom). `capture window` grabs widgets. `select` and `view show` highlight parts in a shared GUI session. Read the resulting PNG to check visual state (LEDs, switches, scope screens).
- **Edits go through the GUI's undo bookkeeping** (`beginUndoStep`, `addCompChange`, `endUndoStep`, ...). That keeps them undoable from both the GUI and the CLI.

### Part states (`src/gui/circuitwidget/statusview.cpp`)
- A scripted core reports its state with `component.setStatus(text, ST_OFF|ST_ON|ST_WAIT|ST_FAULT)` (`ScriptCpu::setStatus`, simulation thread). It keeps the current state, the most severe one since the last frame, and a history of changes with their times, all behind a mutex.
- Once per frame, `ScriptCpu::updateStep()` copies the state to show to its component (`Component::setStatus`). A state more severe than the current one, seen during the frame, is held for 3 frames.
- `InstrumentView` (`settings set instruments`, toolbar) shows or hides instruments (`isInstrument`: scope, logic analyzer, voltmeter, frequency meter, probe) with `setVisible`, together with their leads (`Connector::leadPin()`, also used to draw leads in colour via `Pin::wireColor()` and `Theme::lead()`), lead tunnels (`Tunnel::leadPin()`) and grounds/rails used only by leads. It re-applies after every edit (`Circuit::saveChanges`) and load, and `Node::paint` skips the dot of a junction with a hidden wire.
- `StatusView` (an Updatable, like `ChargeView`, toggled from the toolbar and with `settings set partStatus`) gives each top-level part that reports a `StatusBadge`. That is a child item of the part, with the part's transform cancelled so it draws upright in scene coordinates: the part's own state and those of the scripted cores inside a subcircuit, as pills above the part, plus the outline in the most severe colour. The badges are removed when the simulation stops.
- Built-in parts (BJT, MOSFET, SCR, triac, diac, op amp, voltage regulator, diode, zener, LED, 555, relay, analog mux) report their operating state through a `StateLog` (`statelog.{h,cpp}`, the same event type and sequence as the scripted cores). The part calls `enableStates()` and lists `showStateProp()` (Show State) in its constructor. It asks for `Simulator::addToSettled(this)` in `voltChanged()` and calls `reportState()` in `stepSettled()`, which the simulator calls once the time step has converged, so the times are exact. A part that changes state in `runEvent()` (555, analog mux) reports there directly. Plan, rules (colours, hysteresis, model tolerances) and the remaining parts: `docs/PART_STATES.md`.
- CLI: `read` (`status`, or `state` for built-in parts) and `status <comp>` (history), in `cli_measure.cpp`.

### Event log (`src/gui/circuitwidget/eventlog.cpp`)
- `EventLog` (an Updatable) runs once per frame:
  - It reads new part-state events (`ScriptCpu::statusHistory`, by sequence number) and joins changes within 1 µs into one line.
  - It samples capacitors whose `LogCharge` property is on, from 20 ms means with a 3 % hysteresis: charging, charged, discharging, discharged, over the rating. These times are approximate.
  - It only works while the matching toolbar display is on (`partsActive()`: Show Part States; `capsActive()`: Show Capacitor Charge). Switched on during a run, it catches up silently and logs from then on.
- Lines are written to Messages (`OutPanelText::appendPlain` with a colour and the part's uid; a click selects the part).
- A sequence of line signatures, which carry no numbers, that repeats 3 times is summarised with its period, up to 8 lines per cycle. Plateau lines ("charged", "steady") are left out of the signatures.
- Rate limit: 20 lines per part per wall-clock second.
- All events are kept for the CLI `log` command.
- Checkboxes above Messages and the settings `logParts` / `logCaps` control it.

### Other areas
- `src/gui/editorwidget/` contains the code editor plus compiler/debugger integrations in `debuggers/`.
- `src/gui/dataplotwidget/` contains the oscilloscope and logic-analyzer plots. `src/gui/serial/` contains the serial port and serial monitor.
- Translations live in `resources/translations/*.ts`.
