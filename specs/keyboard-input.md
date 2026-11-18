# Keyboard input to create circuits (epic)

Status: ready for implementation (2026-11-18). All decisions (D1–D15, §6) taken
by the user on 2026-11-18. K1 revised the same day (D11–D15: the keyboard cursor
is independent of the mouse, pan mode, zoom keys).

Goal: build a circuit without the mouse — place parts at a keyboard cursor, type
their values, pick pins and wire them, walk and edit the wire network.

Delivery rule: the epic is split into **10 features (K1–K10)**, implemented in the
order of §5. Each is one PR, built and handed over for the user's own validation
(checklist per feature) before the next one starts. Each also gets automated tests
through the CLI (§4.6).

The user's keyboard is a **Mac AZERTY**: every key below is matched on the
character it produces, never on its position, and validation is done on AZERTY.

---

## 1. What exists today (fork, `main` c0c9a70)

| Area | Today | Where |
|---|---|---|
| Key dispatch | `Circuit::keyPressEvent` (the scene) handles Esc (stop linking / cancel new wire), Ctrl+C/X/V/Z/Y/N/S/O/A/R/L, Delete/Backspace = remove selection, Alt+char = component shortcut, every other char is emitted as `Circuit::keyEvent(char, pressed)` for switches. `m_acceptKeys=false` while a Text component is edited | `circuit.cpp:1146` |
| Arrow keys | Not handled by SimulIDE: the scene ignores them, so `QGraphicsView` (`QAbstractScrollArea`) scrolls = "pans" | Qt default |
| Component shortcuts | User-set per part in *Manage Components* (stored `compSettings: <name>/shortcut`), triggered with **Alt**+char, part placed at the mouse | `componentselector.cpp:248`, `managecomps.cpp` |
| Key-bound parts | `Push` and `Switch` have a `Key` property; they react to `Circuit::keyEvent` (any non-modifier char, also lowercase) | `push_base.cpp:42`, `switch.cpp:88` |
| Moving a selection | Only by mouse drag; the move + undo logic lives inside `Component::mouseMoveEvent` | `component.cpp:264` |
| Wires | `Connector` = one wire pin→pin; its segments are `ConnectorLine`s, a **corner** is the shared end of two segments (not an item). A junction **dot** is a `Node` (a 3-pin `Component`). A net from a part pin through dots is several Connectors | `connector.h`, `node.h` |
| Wire selection | Dots are selectable like parts. Segments with **Ctrl+click** and by rubber band; right click → Remove deletes the whole wire. Corners move with Shift+drag, segments with middle drag | `connectorline.cpp:176,303` |
| Programmatic wiring | CLI `wire add <pinA> <pinB>` routes H/V and inserts a dot when a pin already has a wire | `src/cli/`, `docs/CLI.md` §Wires |
| Grids | parts on 8 px (`toCompGrid`), wires on 4 px (`toGrid`) | `utils.cpp:225` |
| "Principal value" | `Component::m_showProperty` = property shown in the value label (Resistance, Capacitance, …); empty for many parts | `component.h:243` |
| Unit parsing | `getMultiplier("k")`, `DoubProp`/`IntProp` handle unit + multiplier | `utils.h`, `properties/` |
| Theme | Roles (canvas, wire, line, …) with presets; no highlight role | `theme.h` |
| Settings | `AppDialog` = `QTabWidget` (`appTab`, `circTab`, `simTab`) | `dialogs/appdialog.ui` |
| CLI keys | `ui key <char> [down|up|tap]` emits `Circuit::keyEvent` (switches only, not a real QKeyEvent) | `cli_measure.cpp:762` |

Corrections to the epic's assumptions:
- "today only wire connections can be selected": dots *and* wire segments can be
  selected (segments with Ctrl+click or rubber band).
- There is no yellow cursor cross yet; K1 creates it.
- Part shortcuts exist already but need **Alt**; K5 makes them single keys.

---

## 2. Review: conflicts and decisions

### 2.1 Conflicts and how they are resolved

| # | Conflict | Resolution |
|---|---|---|
| C1 | `m` was assigned to both mosfet and opamp | **D1**: opamp = `u`, UJT = `q` |
| C2 | **Shift+Arrow** = move the keyboard cursor *and* the small part step | Context: no selection → the arrows move the cursor (big) / Shift+arrows (small); selection → the arrows move it (big) / Shift+arrows (small). Panning is only in pan mode (D14) |
| C3 | Value input uses `p n u m k M G` as multipliers, the same letters add parts | Only in *edit value* state (entered by a digit or `-`); there all multiplier letters are units and no part is added |
| C4 | `-` was the wiring key and the minus sign of a value | **D6**: `-` is numeric (starts / continues a value, first char only). Wiring is **`_`** |
| C5 | Lowercase letters become part shortcuts, but existing circuits have switches with `Key="a"` etc. | **D7**: `Switch`/`Push` `Key` accepts **one capital A–Z** only, pressed as Shift+letter. Any other saved value is kept in the file but does nothing; opening the part's properties clears it. The Key field gets a description |
| C6 | Tab / Shift+Tab are eaten by Qt's focus chain before any `keyPressEvent` | `CircuitView::event()` intercepts `Key_Tab` / `Key_Backtab` while the canvas has focus |
| C7 | Esc has several meanings | Priority, first match wins: (1) wire being drawn by mouse → cancel it, (2) linking → stop, (3) value edit → revert, stay selected, (4) network mode → back to parts mode, the part selected, (5) unselect all |
| C8 | Backspace in value edit, then delete | **D8**: a value is never empty. When the last typed char is removed, edit state ends and the value from before the edit is shown; the **next** Backspace deletes the part (as today) |
| C9 | Where is the cursor after placing / moving / Enter? | **D11**: the cross is independent of the mouse — it starts centred in the view, a plain left click on empty canvas places it, moving a selection does not move it. **D12/D13**: Enter, and part placement with a part selected, put it a big step right of that part |
| C10 | **Default pin = upper-right** breaks a chain: `r 1k Enter r 2k _` would wire R1.right to R2.right | **D2**, nearest pin: see the *auto pin* rule, §4.3 |
| C11 | After Enter or Esc nothing is selected, so the next part has no previous part to wire from | **D3** (most logical): the controller remembers the **last single-selected part**; unselecting (Esc, Enter, clicking empty canvas) never forgets it. Only deleting that part, or a new / opened circuit, clears it |
| C12 | Pin colour on unselected parts | **D4**: every part shows its selected pin; on unselected parts in the normal pin colour with slightly more contrast (`Theme::pinAccent()`, derived from the line colour, ~1.5× contrast against the canvas) |
| C13 | Wire selection behaviour was not specified | **D5**: `=` toggles *parts* ↔ *network* mode; in network mode Tab walks wires, dots and corners of the network on the selected pin (K9) |
| C14 | "Wire connection" = dots or corners? | **D5**: both, plus whole wires, are steps of the walk. Dots and corners can't be deleted, only complete wires |
| C15 | A wire from a part pin may end on another part (no dot, maybe no corner) | Then the walk is that wire and its corners only; a pin without any wire → message, mode stays *parts* |
| C16 | `<` `>` and digits on Mac AZERTY | `<` is unshifted, `>` = Shift+`<`; Cmd+`>` = Cmd+Shift+`<` (to check: Qt reports `Key_Greater` with Cmd on macOS). **Digits need Shift** on the AZERTY number row — D10: number-row keys count as digits with or without Shift |
| C17 | Space opens properties; could a switch be bound to Space? | No, keys are A–Z (C5) |
| C18 | Typing parts while the simulation runs | Same as the mouse: placing a part keeps running (as Alt+shortcut does today), wiring powers off first (as `closeconnector` does) |

### 2.2 Gaps filled in by this spec

- **Focus and text entry**: the keymap is off while a Text component, a property
  field, a dialog or the code editor has focus (today's `m_acceptKeys`, extended).
- **Undo**: one undo step per placed part, committed value, wire, deleted wire,
  and per burst of arrow presses on the same selection (merged until another key).
- **Auto-scroll**: when the keyboard cursor, a moved selection or a newly selected
  item leaves the view, the view scrolls to keep it visible (margin 32 px).
- **Pins stored by id** (`"R1-lPin"`), not pointers: pinFrom/pinTo and the last
  part survive undo/redo and become empty when the part is deleted.
- **Part shortcut defaults** live in the existing configurable shortcut table, with
  conflict checks (K5).
- **Step sizes** as named constants: big step 64 px (8 grid cells; a resistor is
  ~32 px long), small step 8 px. Tuned at validation.
- **Decimal separator**: both `.` and `,` (AZERTY habit).

### 2.3 Architecture verdict

Implementable cleanly. The one thing to avoid is growing `Circuit::keyPressEvent`
(already a 120-line if-chain) into a state machine: a **`KeyboardController`**
owns the state and keymap and `Circuit` delegates to it (§3). Two extractions
from existing code, worth it on their own:

1. `Circuit::moveSelection(QPointF delta)` out of `Component::mouseMoveEvent`
   (mouse drag and arrows share one move + undo path).
2. `Circuit::connectPins(Pin*, Pin*)` out of the CLI `wire add` (CLI and `_`
   share routing and dot insertion).

Corner selection is the only new kind of selection (corners are not graphics
items): the controller keeps `(connector uid, corner index)` and draws its marker.

---

## 3. Architecture

```
CircuitView (QGraphicsView)          Circuit (QGraphicsScene)
  event(): Tab/Backtab -> scene        keyPressEvent(): if controller->handle(ev) return;
  focusIn/Out -> controller              else old behaviour (Ctrl+..., switch keys)
  mouseMove  -> controller             drawForeground(): controller->paint() (cross, corner marker)
            \                          /
             KeyboardController (new, src/gui/circuitwidget/keyboard/)
               state: Mode {Parts, Network}; EditValue (buffer, comp uid, old value)
                      cursorPos (8 px grid), cursorVisible, mouseHidden
                      pinFrom, pinTo (pin ids), lastPart (uid)
                      network walk (ordered list of wire / dot / corner refs, index)
               keymap: Shortcuts registry (category, action, key, context, handler)
               uses:   Circuit::moveSelection, Circuit::connectPins, NetworkWalk,
                       Component selected-pin API
```

- **`Shortcuts` registry**: one table of `{category, action text, key text,
  context}` for **every** shortcut, including the existing Ctrl ones and the
  toolbar/menu actions (read from their `QAction::shortcut()`). The controller
  dispatches from it and K10 lists it, so the list can't drift.
- **Component API** (in `Component`, not per part):
  - `selectedPin()` / `setSelectedPin(int, bool manual)` / `pinIsManual()`.
    Default pin = topmost in scene coordinates, ties → rightmost. `pinsClockwise()`
    = pins sorted by angle around the part's bounding-rect centre in scene
    coordinates (rotation and flip respected). Not saved in the `.sim1`.
  - `virtual QString valueProperty()` = the property typed values go to; default
    `m_showProperty` if it is a `DoubProp`/`IntProp`, overridden where needed.
- **`Circuit::connectPins(pinA, pinB)`** (K8): wires two pins as the CLI
  `wire add` does — H/V route with the corner chosen by the pins' sides
  (`Pin::side()`), a junction dot on a pin that already has a wire, one undo
  step, the simulation powered off first. The wire helpers it shares with the
  CLI moved to `Circuit` (`splittable`, `netWires`, `backwards`,
  `routePoints`); K9's network walk builds on `netWires`.
- **`NetworkWalk`** (K9): builds the ordered list of wires, dots and corners
  reachable from a pin, without crossing parts.
- **Pin painting**: `Pin::paint` asks the controller for its role
  (`none / selected / from / to`) and draws with `Theme::highlight()` or
  `Theme::pinAccent()`.
- **Theme**: new role `highlightRole` (dark presets yellow `#FFD200`, light
  presets `#E07000`), editable with the other colours; `pinAccent()` derived.

---

## 4. Shared rules

### 4.1 Contexts
1. Canvas not focused, text being edited, a dialog open, a wire being drawn by
   mouse (except Esc) → the controller passes everything on (old behaviour).
2. **EditValue** (K7) → digits, `-` (first), `.`/`,`, multipliers, Backspace,
   Enter, Esc; any other handled key reverts first, then acts.
3. **Network mode** (K9).
4. Otherwise **parts mode**, by selection.

### 4.2 Keymap (target state after K10)

Parts mode:

| Key | Nothing selected | Part(s) selected | Single part selected | EditValue |
|---|---|---|---|---|
| Arrows | move the cross 64 px | move 64 px | move 64 px | – |
| Shift+Arrows | move the cross 8 px | move 8 px | move 8 px | – |
| `h` | pan mode: the arrows (Shift: small) pan, any other key / selection / mouse press ends it | same | same | – |
| `Cmd+=` / `Cmd++` | zoom in (the cross or view centre kept in place) | same | same | – |
| `Cmd+-` | zoom out | same | same | – |
| `Cmd+0` | zoom 100 % | same | same | – |
| Tab / Shift+Tab | part nearest the cursor | next/prev part | next/prev part | revert, then next/prev |
| Esc | – | unselect all | unselect all | revert value |
| Enter | – | – | unselect, cursor 64 px right | commit, unselect, cursor right |
| Space | – | – | properties dialog | – |
| `>` / `<` | – | – | next/prev pin → pinTo | – |
| Cmd+`>` / Cmd+`<` | – | – | next/prev pin → pinFrom | – |
| digit, `-` | – | – | start EditValue (numeric value only) | append (`-` first only) |
| `.` `,` | – | – | start EditValue (`0.`) | append once |
| part letter | add part at cursor | add part | add part | multiplier (C3) |
| `_` | wire pinFrom→pinTo | same | same | – |
| `=` | – | – | network mode (K9) | – |
| Backspace / Delete | – | delete | delete | remove last char (C8) |
| Shift+A–Z | switches (`Key`) | switches | switches | `M`, `G` only |

Network mode (K9):

| Key | Wire selected | Dot or corner selected |
|---|---|---|
| Tab / Shift+Tab | next / previous step of the walk | same |
| Arrows / Shift+Arrows | nothing | move it 64 / 8 px |
| Backspace / Delete | delete the whole wire | nothing |
| `=` / Esc | back to parts mode, the part selected | same |

Ctrl/Cmd shortcuts are unchanged. Alt+char part shortcuts keep working.

### 4.3 Selection change and the auto pin rule (used by K4, K5, K8, mouse)

- A part's pin is **manual** once set with `>`, `<`, Cmd+`>` or Cmd+`<`;
  otherwise it is **auto**.
- Whenever the selection becomes **one part B** by any means (Tab, click, adding a
  part) and `lastPart` A exists and ≠ B:
  - both pins manual → pinFrom = A's pin, pinTo = B's pin;
  - one auto → the auto pin becomes the pin of its part **nearest** to the
    manual one;
  - both auto → the **nearest pair** of pins (one on A, one on B).

  The auto pins are shown as the parts' selected pins. Then `lastPart = B`.
- Auto pins are recomputed when one of the two parts moves, and just before `_`.
- Selecting several parts or nothing does not change pinFrom, pinTo or lastPart.

So `r 1k Enter r 2k _ Enter r 3k _` builds R1.right–R2.left, R2.right–R3.left.

### 4.4 Persistence
Nothing of the controller state is saved in the circuit. Mode resets to parts on
new/open.

### 4.5 Status line
The current mode, the edit buffer and messages ("pin has no wire", "already
connected") show in the status bar only (the cross is hidden while a part is
selected, D11).

### 4.6 Testing hooks (part of K1)
- CLI `ui type <keys...>` sends real `QKeyEvent`s to the circuit view, e.g.
  `ui type r 2 0 0 k Enter shift+Left cmd+> Tab _` (gives the canvas focus
  first). Keys are characters, so tests don't depend on the layout.
- CLI `kbd state` prints JSON: mode, cursor, cursorVisible, selection, pinFrom,
  pinTo (with manual/auto), lastPart, edit buffer, walk list and index.
- Tests in `tests/kbd/test_K<n>_*.py`, harness of `tests/regress/`.
- `capture view` shows the cross, highlighted pins and corner markers.

---

## 5. Features, in implementation order

Each feature: behaviour, implementation notes, **validation checklist** (the
user's manual check; the same items become automated tests where the CLI can see
them). All checks on the Mac AZERTY keyboard.

### K1 — Controller, highlight colour, keyboard cursor
*(epic: "keyboard cursor")*

- While the canvas has focus and **no part is selected**, a cross (11×11 px,
  1.5 px, `Theme::highlight()`) sits on an 8 px grid point: at start-up in the
  middle of the view, moved with the arrows (64 px, Shift 8 px) when nothing is
  selected, or **placed by a plain left click on empty canvas** (D11). A click on
  a part, a wire or a rubber-band drag does not move it.
- The cross does **not** follow the mouse. Any key (not a lone modifier) hides
  the mouse pointer; moving the mouse shows the pointer again (the cross stays).
- A part selected by any means hides the cross; no part selected shows it again,
  where it was. (A selected junction dot or wire segment is not a part: the cross
  shows, and the arrows still move that selection.)
- Canvas loses focus → cross hidden; regains it → cross back where it was.
- **Pan mode** (D14): `h` — the pointer becomes a cross with arrows and the
  arrows (Shift: 8 px) pan the canvas; any other key, selecting a part or a
  mouse press ends it. `h` is not a part shortcut (Manage Components refuses).
- **Zoom** (D15): `Cmd+=` / `Cmd++` in, `Cmd+-` out, `Cmd+0` back to 100 %,
  keeping the cross (or the view centre) in place. On AZERTY, `Cmd+0` is matched
  with or without Shift (physical number-row key).

Implementation: `KeyboardController`, `Shortcuts` registry skeleton, Theme role,
`Circuit::drawForeground`, `CircuitView` focus/mouse/click hooks, `ui type`,
`ui click`, `ui mouse`, `kbd state`.

Validate
- [ ] At start-up the cross is in the middle of the canvas, dark and light theme.
- [ ] The cross does not follow the mouse; a click on empty canvas places it,
      snapped to the grid.
- [ ] Arrows move it 64 px, Shift+arrows 8 px; the view scrolls along.
- [ ] A key hides the pointer; moving the mouse shows it again.
- [ ] Select a part (click, Tab): the cross disappears, parked at the part's
      centre. Unselect: shown there.
- [ ] `h`: pan symbol, arrows pan; another key, a selection or a click ends it.
- [ ] `Cmd+=` / `Cmd++` zoom in, `Cmd+-` out, `Cmd+0` 100 % — cross kept in place.
- [ ] Click the component list → cross gone; click the canvas → back.
- [ ] Highlight colour editable in Settings → Circuit → colours ("Highlight"); the
      Dark preset gives yellow, Classic/Light orange.
- [ ] All existing shortcuts (Ctrl+…, Delete, Esc, switch keys) still work.

### K2 — Moving parts with the arrows
*(epic: "moving parts")*

- Nothing selected: the arrows move the cross (K1 rev). Selection (parts, dots, wire segments,
  as a mouse drag would move them): arrows 64 px, Shift+arrows 8 px.
- Wires follow exactly as in a mouse drag; the cursor follows (C9); auto-scroll.
- Refactor: `Circuit::moveSelection(delta)`, used by `Component::mouseMoveEvent`.

Validate
- [ ] No selection: arrows pan.
- [ ] One resistor selected: Right moves it 64 px, Shift+Right 8 px, wires follow.
- [ ] Several parts + a dot selected move together.
- [ ] Cmd+Z after 5 arrow presses undoes all 5 at once.
- [ ] Mouse drag still works and undoes as before.

### K3 — Pin selection
*(epic: "pin selection")*

- Every part has a selected pin (auto: topmost, then rightmost). Single part
  selected: `>` next pin clockwise, `<` previous; the pin becomes manual and
  pinTo. Cmd+`>` / Cmd+`<` change the selected pin, make it manual and set
  **pinFrom** to it (to wire two pins of the same part; pinTo stays).
- Drawing (D4): selected part → selected pin in `highlight`; unselected parts →
  selected pin in `pinAccent`; the pinFrom pin in `highlight` wherever it is,
  with a small ring to tell it from pinTo.
- Several parts selected: `>`/`<` do nothing. Rotate/flip keeps the same pin.

Validate
- [ ] Place a resistor, select it: right pin in yellow; unselect: same pin
      slightly brighter than the left one.
- [ ] `<` and `>` (Shift+`<`) walk around the pins of a BJT and an opamp.
- [ ] Rotate the part: the same pin stays highlighted.
- [ ] Cmd+`>` then `>` on an opamp: pinFrom (ring) on one pin, pinTo on the next.
- [ ] `kbd state` shows pinFrom/pinTo ids, manual.

### K4 — Part selection with Tab, Esc, Enter
*(epic: "part selection")*

- Tab / Shift+Tab: next/previous part in **reading order** (rows top→bottom by
  part centre, 16 px tolerance, then left→right), wrapping. Nothing selected: the
  part nearest the cursor. Dots are skipped. Applies §4.3. The (hidden) cross
  moves to the selected part's centre, so Esc shows it there.
- Esc: unselect all (C7). Enter: unselect the single part; the cross goes a
  big step beyond its **selected pin**, in the pin's direction (D16; a part
  without pins: a big step right of it, D12). Shift+Enter: the same, but
  beyond the part's **most opposite pin** (D16).
- The selected part is scrolled into view.

Validate
- [ ] Tab cycles all parts in reading order and wraps; Shift+Tab reverses.
- [ ] Tab from R1 to R2 (R2 right of R1): pinFrom = R1.right, pinTo = R2.left.
- [ ] After `<` on R1, Tab to R2: pinFrom is R1's chosen pin, pinTo the R2 pin
      nearest to it.
- [ ] Esc unselects; Enter unselects and moves the cross right.
- [ ] Tab still works after clicking a toolbar button and back in the canvas.

### K5 — Adding parts with single keys
*(epic: "add part to canvas")*

- Default keys: o scope (Oscope), a ammeter, v voltmeter, f freq meter,
  x fixed voltage (FixedVolt), y battery, w wave gen, g ground, b push button,
  s switch, r resistor, p potentiometer, e electrolytic, c capacitor,
  i inductor, d diode, z zener, m mosfet, j jfet, t BJT, q UJT, u opamp, l LED.
  Free lowercase letters: h, k, n.
- Stored as the defaults of the existing per-part shortcut (`<name>/shortcut`);
  a user setting overrides. Manage Components refuses a key already used by a
  part or by the keymap (`_ - = < > . ,`, digits, uppercase letters).
- Plain key → part at the cross (not the mouse), selected, §4.3 applied.
  A part is selected: the new part is placed a big step beyond the selected
  part's **selected pin**, rotated so that its **first pin** points back at
  that pin with the two pin tips 64 px apart on one line (D17), and the cross
  moves to the new part. Nothing selected, but the cross is still where
  Enter left it, beyond a pin: the same continuation from that pin (D17);
  a cross moved elsewhere places the part centred at the cross as usual.
  No pins: a big step right of the part (D13).
  Alt+key keeps working.
- Switch/Push `Key` (D7): one capital A–Z, triggered with Shift+letter. Other
  saved values are ignored at run time; opening the properties clears the field
  (one undo step). The field shows the description *"Capital letter A–Z.
  Press Shift + this letter to operate the switch while the simulation runs."*

Validate
- [ ] Each of the 23 keys places the right part at the cross, selected.
- [ ] `r` with R1 selected: R2 placed, pinFrom/pinTo on the nearest pins.
- [ ] A switch with Key `A` toggles with Shift+A while running; `a` places an
      ammeter.
- [ ] Old circuit with a switch Key `a`: the key does nothing; opening its
      properties empties the field; the description is shown.
- [ ] Manage Components: assigning `r` to another part is refused.

### K6 — Properties with Space
- Single part selected: Space opens its properties dialog (`slotProperties`).
  Closing it returns focus to the canvas.

Validate
- [ ] Space opens / Esc closes the dialog; keyboard keys work right after.
- [ ] Space with 0 or 2+ parts selected does nothing.

### K7 — Typing a part's value
*(epic: "part value input")*

- Single part selected with a numeric `valueProperty()`: a digit (number-row key
  with or without Shift, D10), `-`, `.` or `,` enters **EditValue**. Buffer grammar:
  `[-] digits [(.|,) digits] [multiplier [digits]]` — the last form is `4k7`
  (= 4.7 k). Multipliers `p n u m k M G` (`M` mega, `m` milli); capitals type
  the small ones too (`K` = k, `U` = u), so a shifted keyboard works the same.
  Extra keys after a complete value are ignored. On AZERTY the unshifted 6 key
  (`-`) is the minus and the unshifted 8 key (`_`, the wire key, K8) is never
  a digit: the typed character decides before the physical key does.
- The value label shows the buffer + unit with an edit marker (`200k▏Ω`, in
  `highlight`); the property is **not** changed until Enter. A buffer without a
  multiplier keeps the part's current one (4.7 kΩ + `1 0 0 Enter` = 100 kΩ,
  the label already shows `100▏kΩ` while typing); a typed multiplier replaces it.
- Backspace removes one char; when none is left, edit ends and the value from
  before the edit is shown (D8); the next Backspace deletes the part.
- Enter: set the property (one undo step), leave edit, unselect, cursor right.
- Esc: revert, stay selected. Tab / click / adding a part / any selection change:
  revert, then do that action.
- Values outside the property's limits are clamped by the property; the label
  shows what was set. A negative value on a property that refuses it is clamped
  the same way.
- `valueProperty()` for the K5 parts, checked in code during K7: resistor and
  potentiometer Resistance, capacitors Capacitance, inductor Inductance,
  battery Voltage, fixed voltage Voltage (kept negative), wave gen Freq;
  the others (meters, scope, ground, switches, semiconductors) none → value
  keys do nothing. Checked findings: the zener's breakdown voltage is its
  `Model` enum (BZX… strings), not a number, so a zener takes no typed value;
  battery, fixed voltage and wave gen had no shown value, and now show one
  (`setShowProp`: Voltage, Voltage, Freq) so the typed buffer is visible.
  The default rule is the shown property when it is a double/int property
  (`Component::valueProperty`), so other parts with a numeric shown value
  (resistor DIP, varistor, rail, current source, voltage regulator) take
  typed values too.

Validate
- [ ] `r 2 0 0 k Enter` typed on AZERTY **without Shift** (`r é à à k`): R = 200 kΩ,
      unselected, cross moved right. With Shift: same result.
- [ ] `c 4 u 7 Enter`: 4.7 µF. `c 1 0 0 n Enter`: 100 nF. `c 4 , 7 u`: 4.7 µF.
- [ ] `x - 5 Enter`: fixed voltage −5 V.
- [ ] `r 1 2 Backspace Backspace` shows the old value; one more Backspace deletes.
- [ ] `r 5 6 Esc`: old value, still selected. `r 5 6 Tab`: old value, next part.
- [ ] Cmd+Z after Enter restores the previous value.
- [ ] Digits with a scope selected do nothing.

### K8 — Wiring with `_`
*(epic: "wiring")*

- `_`: connect pinFrom → pinTo with `Circuit::connectPins` (the CLI `wire add`
  path: H/V route, dot inserted if a pin already has a wire). The new wire is
  not selected; the selection stays; pinFrom and pinTo are **cleared** (the
  wired pins lose their highlight and ring), and the selected part's selected
  pin becomes the pin most opposite to its wired one (D18).
- Nothing happens (status message) when pinFrom or pinTo is empty, they are the
  same pin, or already directly connected.
- Simulation running: powered off first, like a mouse wire.

Validate
- [ ] `r 1 k Enter r 2 k _`: R1.right wired to R2.left.
- [ ] Tab between a BJT and a resistor, `>` to the base, `_`: wire to the base.
- [ ] Cmd+`>` then `>` on one opamp, `_`: wire between two of its pins.
- [ ] A pin that already has a wire: a dot inserted on that wire.
- [ ] Cmd+Z removes the wire.

### K9 — Walking the wire network: wires, dots, corners
*(epic: "wire / connection selection")*

- `=` with a single part selected whose selected pin has a wire: **network
  mode**. The wire on that pin is selected (highlighted). No wire → status
  message, stays in parts mode.
- **The walk** (`NetworkWalk`): every wire, dot and corner reachable from the pin
  along wires and through dots, never through a part (other parts' pins end it;
  a tunnel is a part). Each step has a path length from the pin along the wires
  (wire: its start; corner, dot: their position). Steps sorted by that length;
  ties: wire, corner, dot, then scene position. The first step (length 0) is the
  pin's wire.
- Tab / Shift+Tab: next / previous step, wrapping; the item is scrolled into view.
- A selected **wire**: all its segments highlighted. Arrows do nothing.
  Backspace/Delete deletes the whole wire (one undo step); the walk is rebuilt
  and the selection goes to the nearest remaining step, or back to parts mode if
  none is left.
- A selected **dot**: arrows move it 64 / 8 px, its wires reroute as in a drag.
  Backspace does nothing.
- A selected **corner** (new, drawn by the controller as a small square in
  `highlight`): arrows move it; the two segments meeting there move with it so
  the wire stays horizontal/vertical (the middle-drag segment move applied to
  each, adding a segment where one ends on a pin, as the drag does). Backspace
  does nothing.
- After a move the walk is rebuilt (corners may merge); the selection stays on
  the moved item.
- `=` or Esc: back to parts mode, the part (and its pin) selected again.
- Mouse wire handling is unchanged (Ctrl+click, right click → Remove).

Validate
- [ ] R1 and R2 to a dot, the dot to R3: select R1, `=` → R1's wire highlighted.
- [ ] Tab: its corners, then the dot, then the other wires and their corners,
      nearest first; Shift+Tab goes back; it wraps.
- [ ] On a dot: Shift+Right moves it, wires follow; Backspace does nothing.
- [ ] On a corner: arrows move it, the wire stays H/V; Backspace does nothing.
- [ ] On a wire: arrows do nothing; Backspace deletes it, Cmd+Z restores it.
- [ ] `=` back to parts: R1 selected, its pin unchanged.
- [ ] `=` on a pin without a wire: message, still parts mode.

### K10 — Shortcut list in Settings
*(epic: "listing shortcuts")*

- New tab **Shortcuts** in `AppDialog`: read-only, scrollable table grouped by
  category (File, Edit, View, Selection, Moving, Pins & wiring, Values, Network,
  Parts, Switches). Columns: Action, Key (⌘ ⇧ ⌥ symbols on macOS).
- Built from the `Shortcuts` registry + the part shortcuts in effect (defaults
  and user overrides) — nothing hand-typed.
- Text at the top: part shortcuts are set in the component list → right click →
  *Manage Components* → Shortcut column (one lowercase letter); capital letters
  are reserved for the `Key` of switches and push buttons (Shift+letter).

Validate
- [ ] Every key of §4.2 and every Cmd shortcut appears once, in its category.
- [ ] Change a part shortcut in Manage Components → the list shows it next time.
- [ ] Scrolls on a small window; readable in dark and light theme.

---

## 6. Decisions and open questions

Decisions (user, 2026-11-18):
- **D1** opamp `u`, UJT `q`.
- **D2** pinTo = nearest pin; generalised to the auto pin rule (§4.3) so that
  chains work in both directions.
- **D3** unselecting keeps the last part (only deleting it or a new/opened
  circuit forgets it).
- **D4** selected pin shown on every part, slightly more contrast when unselected.
- **D5** `=` toggles parts ↔ network mode; Tab walks wires, dots and corners
  nearest first; only whole wires can be deleted; arrows move dots and corners,
  not wires.
- **D6** `-` is numeric; wiring is `_`.
- **D7** switch keys: one capital A–Z; other old values inert, cleared when the
  properties are opened; the field has a description.
- **D8** a value is never empty: back to the value before editing, then
  Backspace deletes.
- **D9** Mac AZERTY keyboard.

- **D11** (K1 rev) The keyboard cursor is independent of the mouse: centred at
  start-up, placed by a plain left click on empty canvas, moved by the arrows
  (nothing selected), shown only while the canvas has focus and no part is
  selected. Whatever selects a single part — Tab, a mouse click, the CLI —
  parks the hidden cross at that part's centre, and it follows a moved
  selection (arrow moves and the rotation alignment shift move it along), so
  Esc shows it where the part ended up.
- **D12** (K1 rev) Enter puts the cross a big step right of the part it unselects
  (not of the old cross position). *Replaced by D16 for parts with pins; still
  the rule for parts without pins.*
- **D13** (K1 rev) Placing a part with a part selected puts the new part (and the
  cross) a big step right of the selected part. *Replaced by D17 when the
  selected part has a selected pin; still the rule without pins.*
- **D16** (K4 rev) Enter leaves a part towards its **selected pin**: the cross
  goes a big step (64 px) beyond that pin's tip, in the pin's direction. This
  applies everywhere Enter leaves a part — plain Enter, and Enter after a
  committed typed value (K7). **Shift+Enter** does the same from the part's
  most opposite pin: the pin whose direction is most opposite to the selected
  pin's (the dot product, exact for the four directions); ties → the one
  pointing more downwards; still tied → the nearest to the selected pin. No
  other pin: as Enter.
- **D17** (K5 rev) Placing a part with a part selected continues the chain: the
  new part's **first pin** (the part's pin 0: resistor lPin, BJT collector,
  op amp inNinv, fixed voltage outnod) points back at the selected pin of the
  selected part, both pin tips on one line 64 px apart, the new part turned
  to match — rotated, except when the rotation would be 180°/270° (upside
  down): then a **flip** (horizontal for a horizontal first pin, vertical for
  a vertical one) reaches the same side with the labels upright. The cross
  goes to the new part. **Enter remembers the pin it left from**: with
  nothing selected, a placement continues from that pin while the cross is
  still at the spot beyond it (a cross moved elsewhere: the part is centred
  at the cross, unrotated).
- **D18** (K4/K5 rev) The chain keeps flowing: the new part's **selected pin**
  is the one nearest to the outgoing pin it was placed at (there is none: the
  §4.3 pairing), and pinFrom is that outgoing pin. After `_` wires a pin of
  the selected part, that part's selected pin becomes the pin **most
  opposite** to the wired one (D16's rule), so the next Enter or placement
  continues out the other side — and pinFrom / pinTo are **cleared**: a made
  wire's pins are no longer the wiring pair, so nothing stays highlighted.
- **D19** (K4 rev) Rotating a selected part (`Cmd+R`/`Cmd+Shift+R`) with an
  outgoing pin (pinFrom) on **another** part also moves it: its pin closest to
  the outgoing pin is brought onto that pin's row or column (the smaller
  offset is eliminated; the shape is irrelevant — any pin can land off the
  line), so a straight wire stays possible. pinFrom on the rotated part
  itself, or none: the plain rotation. The move is one undo step, wires
  following.
- **D14** (K1 rev) `h` starts pan mode (pointer: cross with arrows; arrows pan;
  any other key, a selection or a mouse press ends it). `h` is not a part shortcut.
- **D15** (K1 rev) `Cmd+=`/`Cmd++` zoom in, `Cmd+-` out, `Cmd+0` 100 %, cross kept
  in place.
- **D10** Mac AZERTY number row: while a single part with a numeric value is
  selected (or in EditValue), the number-row keys count as digits with or without
  Shift, matched by physical key (macOS `nativeVirtualKey` = `kVK_ANSI_0…9`, the
  number row on every layout). Their unshifted characters (`& é " ' ( § è ! ç à`)
  are used by no other shortcut.
