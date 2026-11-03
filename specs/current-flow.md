# Spec: current flow animation

Status: **implemented** (all phases). The open questions below are decided:
threshold adjustable (`currentMin`, default 1 µA), conventional current only,
app setting. Target: SimulIDE 1.1.0-SR2 (this fork).

## 1. Goal

While the simulation runs, show current flowing through the wires as dots moving
along them:

- **Direction:** the dots move in the direction of the current. The default is
  conventional current, from + to −.
- **Speed:** faster dots mean more current. A log scale keeps both µA and A
  visible.
- **No dots:** a wire carrying no significant current (below 1 µA) shows no
  dots.

It should be possible to see a capacitor charging and discharging, which branch
of a divider carries more current, or that a probe draws nothing. The CLI
exposes the same numbers so they can be tested and used in collaborative
sessions.

### Out of scope for the first version

- Dots flowing *through* components (some simulators draw them through
  resistors and so on). Only wires are animated.
- Colouring wires by voltage on a continuous scale. The existing Animate mode
  already colours wires above or below 2.5 V.
- Electron-flow direction (− to +). It could be a later option (section 9).
- Bus wires and wires inside subcircuits (they're not visible anyway).

## 2. What exists today

- **Animate** is a circuit setting (`Circuit::setAnimate`, saved in the
  `.sim1` header, toggled in `AppDialog`). It colours wires red or blue
  depending on whether their net is above or below 2.5 V
  (`ConnectorLine::paint`). It also colours digital pins by state and draws
  in/out arrows on them (`Pin::paint`). It doesn't show current at all.
- **No currents are stored.** The solver works with node voltages only. Each
  net is one `eNode` with one voltage, so a single wire has no current of its
  own in the model.

## 3. How the model allows it

Every element connects to a net through an `ePin`. It describes itself to that
net as a Norton equivalent, registered in the net's `eNode`:

- `ePin::stampAdmitance(g)`: a conductance `g` from this net to the pin's
  "other side" node (`ePin::m_enodeComp`). The other side is ground (voltage 0)
  when there's no node, or for an `IoPin`'s internal `m_gndEnode`.
- `ePin::stampCurrent(i)`: a current source `i` injected into the net. Voltage
  sources and `IoPin` outputs use this: `stampVolt(V)` is `stampCurrent(V·g)`.
- `ePin::addSingAdm(node, g)` / `stampSingAdm(g)`: a one-sided coupling to
  another node, written only off the matrix diagonal. Only coupled coils use it
  (`e-coil.cpp`).

The values are kept in the `eNode` as linked lists of `Connection{ epin, node,
value }`: `m_firstAdmit`, `m_firstCurrent` and `m_firstSingAdm`. Node numbers
map to `Simulator::m_eNodeList[n-1]`, and node 0 is ground.

Reactive elements use a companion model: a capacitor becomes `g` plus `i` for
each step. Non-linear elements re-stamp on every iteration. So after a step has
been solved, the stored `g` and `i` values are exactly what produced the node
voltages. That makes the current through each pin well defined.

### Pin current

Define the current flowing **out of the net into the element** through pin `p`
on net `N`. Take each term only if `p` registered it:

```
I(p) =  g_p · ( V(N) − V(other_p) )      // admittance connection of p
      − i_p                               // current source of p
      − Σ_k  s_pk · V(node_k)             // single admittances of p
```

- **Signs:** the solver's node equation is
  `Σg·V(N) − Σg·V(other) − Σ s·V(node) = Σ i`. Writing out the current per pin
  gives the signs above. The sum of `I(p)` over all pins of a net is then 0,
  which is Kirchhoff's current law. That's the first test (section 8).
- **Nothing registered:** a pin with no entries in any list has `I(p) = 0`, and
  is called **passive** here. Examples:
  - a subcircuit package pin,
  - a `Tunnel`,
  - a `Node` junction pin,
  - a pin whose element connects both its pins to the same net. In that case
    `eNode::addConnection` drops the entry, and the element's net current
    really is 0.

## 4. Engine changes (`src/simulator/`)

1. `double eNode::pinCurrent( ePin* p )` implements the formula above, using
   the three lists.
   - For the admittance term, `V(other)` is `p`'s `m_enodeComp->getVolt()`, or
     0 when there's no other node. Add a getter `ePin::getEnodeComp()`.
   - For single admittances, `V(node_k)` comes from `Simulator::self()`'s eNode
     list. Add a small accessor `Simulator::eNodeVolt(int n)`, where node 0 is
     0 V.
   - Each call walks the linked lists. That's short per net and runs only for
     visible pins, at the frame rate.
2. `double ePin::getCurrent()` returns `m_enode ? m_enode->pinCurrent(this) : 0`.
   It's for the CLI and for tests.

No change to how anything is solved or stamped. The model's accuracy and speed
are unaffected when the feature is off.

## 5. Wire currents (`src/gui/circuitwidget/`)

### The graph of a net

On screen, a net (one `eNode`) is drawn as:
- **vertices:**
  - component pins (`Pin`, a leaf, with current `I(p)`),
  - junctions (`Node` components, each with 3 pins at one point; they merge
    into one vertex),
- **edges:** `Connector`s, each joining its `startPin()` vertex to its
  `endPin()` vertex.

A net can also be made of several separate drawn pieces, joined electrically
by tunnels, subcircuit pins or hidden pins.

### Algorithm, per net piece

1. Build the vertices and edges from the eNode's visible `Pin`s and their
   connectors (`Pin::connector()`), with breadth-first search across the
   `Node` junctions.
2. **Injection:** each leaf pin injects `J(p) = −I(p)`, the current flowing
   *into* the net from its element. Junction pins inject nothing.
3. **Residual:** `R = Σ J` over the piece. If the piece is the whole net and
   every pin is visible, `R ≈ 0`. If not, the missing current flows through
   passive pins in the piece (tunnels, subcircuit package pins), so `R` goes
   to them:
   - **One passive pin:** it gets `J = −R`. That's exact.
   - **Several passive pins:** split `−R` evenly among them and flag the piece
     as *approximate*. The CLI reports the flag.
   - **None, and |R| is above 1 % of the largest |J| plus 1 nA:** flag the
     piece as *inconsistent*. That's a bug signal, logged once.
4. **Spanning tree:** pick a root (a passive pin if there is one, otherwise
   any leaf), then breadth-first search.
   - The current through a tree edge, from parent to child, is the sum of `J`
     in the child's subtree.
   - That's one pass from the leaves up: O(pins + wires) per net.
5. **Loops:** an edge that's not in the tree closes a loop of ideal wires,
   where the split is undefined. It gets current 0. The drawing still
   conserves current, because the tree edges carry all of it.

The result is a signed current for each `Connector`, positive from
`startPin()` to `endPin()`.

### When it runs

- **Building the graph:** once when the simulation starts, cached in a
  `CurrentFlow` helper. Every structural edit (adding or removing components or
  wires) already stops the simulation, so the topology can't change while it
  runs. Moving a part changes the geometry but not the topology. The cache is
  dropped on `Simulator` change (`circuit new`/`open`) and when the simulation
  stops.
- **Computing currents:** in an `Updatable::updateStep()`, which
  `Simulator::timerEvent` calls after `m_CircuitFuture.waitForFinished()` and
  before the next `runCircuit`. The worker is idle then, so reading the
  `Connection` values is race-free. That's stricter than the existing Animate,
  which reads voltages while the worker runs. The result is a snapshot that
  painting can read at any time.

## 6. Rendering

- **Dots:** each `Connector` gets a phase `φ` in pixels. On every frame,
  `φ += v(I)·Δt`, where `Δt` is the real time since the last frame. Only the
  sign of `I` sets the direction.
- **Spacing:** dots sit every 16 px along the whole connector path, across its
  segments. Each `ConnectorLine` knows its starting offset along the connector,
  so dots flow smoothly round corners. `ConnectorLine::paint` draws the dots
  that fall within its segment, at `offset + φ mod 16`.
- **Opacity:** brighter dots mean more current, as in EveryCircuit
  ("moving dots of various speed and brightness"). Opacity ramps from 0.3 at
  the threshold to 1.0 two decades above it, so dots fade in near the
  threshold instead of popping in.
- **Speed:**
  - Below the threshold, nothing is drawn.
  - Above that, `v = k·(1 + log10(|I| / 1 µA))` px/s, capped. With k ≈ 8 px/s,
    1 µA moves at 8 px/s, 1 mA at 32 px/s and 1 A at 56 px/s.
  - Tune k on the example circuits. Dots should never move more than half a
    spacing per frame, so the motion can't look reversed (wagon-wheel effect).
- **Look:** filled circles of radius 1.6 px in amber, `QColor(255,190,0)`,
  with a thin dark outline so they stay visible on both the dark wires and the
  red/blue Animate colours. They're drawn above the wire line in the same item.
- **Repainting:**
  - Each frame, call `update()` only on lines whose connector has |I| above the
    threshold.
  - A connector whose current dropped below the threshold gets one more
    `update()` so its dots disappear.
  - When the simulation is paused, the phase stops advancing and the dots stay
    still but visible. When it stops, the dots are cleared.
- **Frame rate:** the GUI timer rate (the `fps` setting). There's no separate
  timer.
- **`capture circuit`:** draws whatever is on screen, so the dots appear in
  PNGs. A still image can't show direction or speed; for those, use
  `wire current` (section 7).

## 7. UI, settings and CLI

- **Setting:** "Show current flow" (`Circuit/currentFlow`, an app setting in
  `simulide.ini`, default off). It's a display preference, not a circuit
  property, so it's not saved in `.sim1` files.
  - **GUI:** a checkbox in `AppDialog` next to Animate, plus a toggle button on
    the circuit toolbar next to the power button. It's a common thing to switch
    on and off while watching a circuit.
  - **CLI:** `settings set currentFlow true|false`, in the circuit group of
    `cli_settings.cpp`.
- **New CLI command `wire current [wire ...]`.** With no arguments, it lists
  every wire.
  - Columns: `uid`, `from`, `to`, `current` in A (signed: positive means from →
    to), and `flag` (`approximate` or `inconsistent`, only when set).
  - It works when the setting is off: it computes the snapshot on demand, using
    `CliTicker::runIdle` so that it runs while the worker is idle.
- **`probe` and `comp get` while running:** add each pin's `current` next to
  its `voltage`, using `ePin::getCurrent()`, as the current *into* the
  component.
- **`read <comp>`:** no change. It already reports component currents where the
  component computes them.
- **Docs:**
  - `docs/CLI.md`: the new command, the setting, and the extra `current`
    fields.
  - `docs/COLLABORATION.md`: one line on using `wire current` to confirm a
    capture.

## 8. Verification

The tests go in `tests/cli/smoke.sh` and run headless through the CLI.

1. **Kirchhoff at every junction.** Battery, three resistors in parallel and a
   ground. The wire currents into each junction add up to 0 within 1e-9 A. The
   sum of the branch currents equals the battery current.
2. **Agreement with Ohm's law.** In a series resistor and LED chain, every wire
   carries the same current, which equals `read R1`'s current within 0.1 %.
3. **Sign and direction.** Swap a wire's start and end, by drawing it the other
   way. The reported current flips sign, but the physical direction (the pins
   it flows between) stays the same.
4. **Reactive part.** In an RC circuit, the capacitor wire's current is
   positive into + while charging and changes sign after the source is
   switched off.
5. **Passive pins.** A net split by a pair of tunnels: the residual goes to the
   tunnel pins, and the current is the same on both sides.
6. **Loop.** Two parallel wires between the same two points: one carries all
   the current, the other carries 0, and nothing is flagged as inconsistent.
7. **Subcircuit and MCU.** Arduino LED example: the current out of the board's
   pin wire matches the LED current.
8. **Visual check, GUI.** With the 555 fader sample
   (`samples/555_astable_led_fader.sim1`), the dots should:
   - flow into C1 while it charges,
   - reverse direction while it discharges,
   - speed up in the LED branch as it brightens,
   - stay still (draw nothing) on unconnected pins.
9. **Performance.** On the largest example circuit, the frame rate and the
   simulation speed (`sim status`) with the setting on stay within 5 % of off.

## 9. Open questions

- **Direction:** conventional current (+ to −). Electron flow not added.
- **Scale:** adjustable threshold, default 1 µA (`currentMin`).
- **Where the setting lives:** app setting (`simulide.ini`).
- **Later:** dots through two-pin components. The current is already known
  (`I(p)` at either pin), so this is mostly drawing.

## 10. Plan

| Phase | Content | Done when |
|---|---|---|
| 1 | `eNode::pinCurrent`, `ePin::getCurrent`, the `CurrentFlow` graph and solver, `wire current`, current in `probe`/`comp get` | Tests 1–7 pass |
| 2 | Dot rendering, phase per connector, repainting only active lines | Test 8 looks right in the GUI |
| 3 | Setting, `AppDialog` checkbox, toolbar toggle, CLI setting, docs | Toggle works from GUI and CLI; docs updated |
| 4 | Tuning (speed constant, colours) and the performance check | Test 9 passes |

Estimated effort: about a day in total. Phase 1 is half of it.
