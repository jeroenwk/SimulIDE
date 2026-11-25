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
| C4 | `-` was the wiring key and the minus sign of a value | **D6**: `-` is numeric (starts / continues a value, first char only). Wiring is **`_`**, later **`w`** (D20) |
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
2. `Circuit::connectPins(Pin*, Pin*)` out of the CLI `wire add` (CLI and `w`
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
- **Modules in `src/gui/circuitwidget/keyboard/`** (split before K9, review of
  #83–#88): `KeyboardController` keeps the state that spans contexts (cursor,
  focus, pointer, pan, selection rule, pinFrom/pinTo, the chain's leave pin) and
  dispatches `keyPress` by context in the §4.1 order — EditValue, network mode,
  parts mode. `ValueEditor` (K7) owns the typed value: its grammar, the label
  while typing and the commit (one undo step); `key()` answers *notMine /
  handled / committed* and the controller reverts or leaves the part.
  `PinChain` is the pin geometry of chains (D16–D19: pin direction, nearest and
  most opposite pin, facing placement, leave position), pure functions on scene
  positions. K9 adds `NetworkWalk` the same way: a class owning the walk and
  its index, its keys tried at the "network mode" slot of `keyPress`, painting
  the corner marker through `KeyboardController::paint`.
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
| Arrows | move the cross 8 px (D23) | move 8 px | move 8 px | – |
| Cmd+Arrows | move the cross 64 px | move 64 px | move 64 px | – |
| Shift+Arrows / Cmd+Shift+Arrows | selection rectangle from the cross, 8 / 64 px steps (K11) | move 8 / 64 px | move 8 / 64 px | – |
| `h` | pan mode: the arrows (Shift: small) pan, any other key / selection / mouse press ends it | same | same | – |
| `Cmd+=` / `Cmd++` | zoom in (the cross or view centre kept in place) | same | same | – |
| `Cmd+-` | zoom out | same | same | – |
| `Cmd+0` | zoom 100 % | same | same | – |
| `Opt+Cmd+0` / `Ctrl+Cmd+0` | zoom to fit all / the selection (toolbar keys) | same | same | – |
| Tab / Shift+Tab | part nearest the cursor | next/prev part | next/prev part | revert, then next/prev |
| Esc | – | unselect all | unselect all | revert value |
| Enter | – | – | unselect, cursor 64 px right | commit, unselect, cursor right |
| Space | – | – | properties dialog | – |
| `>` / `<` | – | – | next/prev pin → pinTo | – |
| Cmd+`>` / Cmd+`<` | – | – | next/prev pin → pinFrom | – |
| digit, `-` | – | – | start EditValue (numeric value only) | append (`-` first only) |
| `.` `,` | – | – | start EditValue (`0.`) | append once |
| part letter | add part at cursor | add part | add part | multiplier (C3) |
| `w` | wire pinFrom→pinTo | same | same | revert, then wire |
| `=` | network mode from the dot, corner or wire nearest the cross (K9c) | – | network mode (K9) | – |
| `+` | draw a wire from the wire under the cross (K9b) | – | – | – |
| Backspace / Delete | – | delete | delete | remove last char (C8) |
| Shift+A–Z | switches (`Key`) | switches | switches | `M`, `G` only |

Network mode (K9):

| Key | Wire selected | Dot or corner selected |
|---|---|---|
| Tab / Shift+Tab | next / previous step of the walk | same |
| Arrows / Cmd+Arrows | nothing | move it 8 / 64 px (D23) |
| Backspace / Delete | delete the whole wire (the marked wires, if any) | the marked wires, if any |
| Enter | mark / unmark it (K12) | message |
| `*` | mark the walk's net; all marked: unmark (K12) | same |
| Space | wire colours dialog: marked wires, else this one (K12) | marked wires, if any |
| `=` / Esc | back to parts mode, the part selected | same |

Drawing a wire (K9b):

| Key | Action |
|---|---|
| Arrows / Cmd+Arrows | move the cross (the wire's end) 8 / 64 px (D23); the wire follows H/V |
| `=` | fix a corner at the cross |
| Backspace | remove the last fixed corner |
| Enter | connect to the free pin or the wire within 8 px of the cross |
| Esc (or a mouse press) | cancel: the wire, its dot and the split go |

Ctrl/Cmd shortcuts are unchanged. Alt+char part shortcuts keep working. Toolbar
keys (K9g): `Cmd+,` settings, `Cmd+P` export, F5 start / stop, F6 pause / resume,
F7 current flow, F8 capacitor / inductor display, F9 part states, F10 instruments,
F12 noise — anywhere in the window.

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
- Auto pins are recomputed when one of the two parts moves, and just before `w`.
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
  middle of the view, moved with the arrows (8 px; Cmd 64 px, D23) when nothing is
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
- [ ] Arrows move it 8 px, Cmd+arrows 64 px (D23); the view scrolls along.
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
  as a mouse drag would move them): arrows 8 px, Cmd+arrows 64 px (D23; was 64 px,
  Shift 8 px).
- Wires follow exactly as in a mouse drag; the cursor follows (C9); auto-scroll.
- Refactor: `Circuit::moveSelection(delta)`, used by `Component::mouseMoveEvent`.

Validate
- [ ] No selection: arrows pan.
- [ ] One resistor selected: Right moves it 8 px, Cmd+Right 64 px, wires follow.
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
  x fixed voltage (FixedVolt), y battery, n wave gen (was w, D20), g ground, b push button,
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
  (`-`) is the minus and the unshifted 8 key (`_`) is never
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
  battery, fixed voltage and wave gen have no shown value: they override
  `valueProperty()` (Voltage, Voltage, Freq) instead of getting a default
  `ShowProp` — an empty `ShowProp` is not saved, so a shown default would show
  up on every older circuit and could never be hidden for good (review of
  #86). While a value is typed the value label is shown even when hidden;
  Enter also turns it on (`ShowProp` = the typed property, same undo step),
  a revert hides it again.
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

### K8 — Wiring with `w` (was `_`, D20)
*(epic: "wiring")*

- `w`: connect pinFrom → pinTo with `Circuit::connectPins` (the CLI `wire add`
  path: H/V route, dot inserted if a pin already has a wire). The new wire is
  not selected; the selection stays; pinFrom and pinTo are **cleared** (the
  wired pins lose their highlight and ring), and the selected part's selected
  pin becomes the pin most opposite to its wired one (D18).
- Nothing happens (status message) when pinFrom or pinTo is empty, they are the
  same pin, or already directly connected.
- Simulation running: powered off first, like a mouse wire.

Validate
- [ ] `r 1 k Enter r 2 k _`: R1.right wired to R2.left.
- [ ] Tab between a BJT and a resistor, `>` to the base, `w`: wire to the base.
- [ ] Cmd+`>` then `>` on one opamp, `w`: wire between two of its pins.
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
  ties: dot, wire, corner (*a dot before the wires leaving it*, as the
  validation below walks — the first draft said wire, corner, dot), then
  scene position. The first step (length 0) is the
  pin's wire.
- Tab / Shift+Tab: next / previous step, wrapping; the item is scrolled into view.
- A selected **wire**: all its segments highlighted. Arrows do nothing.
  Backspace/Delete deletes the whole wire (one undo step); the walk is rebuilt
  and the selection goes to the nearest remaining step, or back to parts mode if
  none is left.
- A selected **dot**: arrows move it 8 px, Cmd+arrows 64 px (D23), its wires reroute as in a drag.
  Backspace does nothing.
- A selected **corner** (new, drawn by the controller as a small square in
  `highlight`): arrows move it; the two segments meeting there move with it so
  the wire stays horizontal/vertical (the middle-drag segment move applied to
  each, adding a segment where one ends on a pin, as the drag does). Backspace
  does nothing.
- After a move the walk is rebuilt (corners may merge); the selection stays on
  the moved item.
- `=` or Esc: back to parts mode, the part (and its pin) selected again.
- **Any other key** ends network mode as Esc does, then acts in parts mode
  (Cmd+Z, a part letter, ...): the walk is never used stale. `h` pans and keeps
  the walk. A mouse press or a selection made otherwise ends it (no reselect).
- The step is drawn by the controller in `highlight`: the wire traced, a ring
  round a dot, a small square on a corner (the scene's own selection grey was
  too faint to read as highlighted). Wire and dot are also selected in the
  scene.
- Done (`feature/kbd-k9`): `NetworkWalk` (`keyboard/networkwalk.{h,cpp}`),
  steps kept by uid (+ corner index) and rebuilt after every move or delete;
  `tests/kbd/test_K9_network.py`.
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

### K9b — Drawing a new wire from a wire
*(user request 2026-11-19, after K9)*

- `+` (Shift+`=` on AZERTY and QWERTY) with the cross shown (nothing selected)
  and within 8 px of a straight wire segment: a **dot** goes on that wire at
  the cross (a corner within 8 px is used instead, as a mouse click on a wire
  does) and a **new wire starts there** — the mouse path (`connectToWire`), so
  the split and the new wire are one undo step. No wire near, or within 8 px of
  the wire's own end (a pin or a dot, where no dot can go): a message.
- **Moving the cross traces the wire**: the arrows move the cross (not snapped
  to the cross grid: the wire keeps the grid of the dot it started on) and the
  wire follows as an H/V L from its last fixed point, its first leg turning off
  the last segment (off the source wire at the start). A mouse move does not
  reroute it.
- `=` **forces a corner** at the cross: the route so far becomes fixed.
  Backspace removes the last fixed corner (added: a cheap way back).
- **Enter connects** when the cross is within 8 px of a **free pin** (taken
  first; the last leg does not enter the pin from behind) or of a **wire** (a
  dot there, as the start). Nothing near: a message, still drawing. The route is
  set before the wire is closed, so redo restores it.
- **Esc** or a mouse press cancels: the wire, its dot and the split are undone.
- While drawing, all other keys are ignored (zoom works); the cross stays shown.
- `kbd state` reports `drawing` (fixed points, the wire's current points).
- Done (`feature/kbd-k9b`): `KeyboardController::plusKey` / `drawKey` /
  `finishDraw`; `tests/kbd/test_K9b_draw.py`.

Validate
- [ ] Two parts wired; click under the wire, `+`: a dot on the wire, a new wire
      starting there.
- [ ] Arrows: the wire follows the cross, horizontal / vertical.
- [ ] `=`, then more arrows: the corner stays; Backspace removes it.
- [ ] Cross on a free pin, Enter: wired to it. Cross on another wire, Enter:
      a dot there.
- [ ] Cmd+Z removes the wire and both dots in one step.
- [ ] Esc while drawing: the circuit as before.

### K9c — Network mode with nothing selected
*(user request 2026-11-19, after K9b)*

- `=` with nothing selected: network mode from the **dot, corner or wire nearest
  to the cross**. A wire is always at least as near as the dots and corners on
  it, so the choice is by distance with a preference: a dot counts 0 px extra,
  a corner 4, a wire 8 — a dot or corner next to the cross beats the wire it is
  on, a wire under the cross beats a dot further along it. No wire in the
  circuit: a message.
- The walk is the whole net of that item (never through a part); path lengths
  from the dot, or from the wire's start for a wire or corner; the item itself
  is selected first. `NetworkWalk::nearestItem`, `startFrom`, `buildFrom`.
- `=` / Esc: back to parts mode with nothing selected and the cross shown at
  the step that was selected. Deleting the wire it started from: the walk
  goes on from the item nearest to it (anywhere), or back to parts mode when
  the circuit has no wire left.
- `kbd state` `network.pin` is "" for such a walk.

Validate
- [ ] Nothing selected, cross next to a dot, `=`: the dot selected; Tab walks.
- [ ] Next to a corner: the corner; on the middle of a wire: the wire.
- [ ] Esc: nothing selected, the cross at the item.

### K9d — Cmd+click on a wire, corner or dot
*(user request 2026-11-19, after K9c)*

- Cmd+click (Ctrl in Qt) on a **wire** selects the whole wire as network mode
  does (highlighted, `NetworkWalk::startFrom`); within 6 px of a **corner**, the
  corner; on a **dot**, the dot. The canvas takes the keys, so Tab, arrows and
  Backspace go on from there. Cmd+click on a part still toggles it in the
  selection.
- It replaces upstream's Ctrl+click on a wire, which toggled **one segment** in
  the selection: Delete and Copy then took the whole wire anyway
  (`Circuit::removeItems`, `copy`), and a selected segment only moved rigidly
  with a dragged group of parts — the one use that goes (a rubber band still
  selects segments). Moving a segment (middle-button drag) and a corner
  (Shift+drag) does not depend on selection and is unchanged.
- A walk started by the mouse does not scroll: scrolling under the pressed
  button sends the wire a mouse move, which dragged its segment (found while
  testing). `ui click x,y --cmd` (CLI) clicks with Cmd held.

Validate
- [ ] Cmd+click a wire: the whole wire highlighted; Tab walks on.
- [ ] Cmd+click near a corner: the corner; arrows move it.
- [ ] Cmd+click a dot: the dot. Cmd+click a part: toggled as before.

### K9e — The component list from the keyboard
*(user request 2026-11-19, before K10)*

- `k`: place the part **selected in the component list** (left pane), as a part
  letter places its part (`KeyboardController::placePart`, shared with the
  letters): at the cross, or continuing the chain from the selected part's pin
  (D17). A subcircuit gets its logic symbol, as a drop from the list does.
  Nothing selected in the list: a message. `k` is reserved like `h` and `w`
  (Manage Components refuses it); during a value edit it is still the kilo
  multiplier.
- `:` selects the **next row** of the list, `/` the **previous** one
  (`ComponentSelector::stepPart`), as the list shows them: **group rows
  included**, and a collapsed group is **not opened** — its parts are not
  shown, so they are skipped (user review 2026-11-19; the first version skipped
  groups and opened them). Rows hidden in Manage Components or by the search box
  are skipped; the row is scrolled into view; wrapping at both ends. The canvas
  keeps the keyboard. On AZERTY `:` and `/` are one key (`/` with Shift).
- `k` on a **group** row expands or collapses it; on a part row it places the
  part.
- `kbd state` reports `libraryPart` (type, name), or `libraryGroup` (name,
  expanded) on a group row.
- A headless server no longer saves the window, splitters and group states on
  quit (`MainWindow::closeEvent`): it shares the user's settings, and its
  offscreen window and test toggles are not the user's.
- The list scrolls so the selected part is always in view
  (`ComponentSelector::showItem`): `scrollToItem` left it below the visible
  rows while stepping down (the scroll bar did not move, user report
  2026-11-19), so the scroll bar is stepped until the row fits.

Validate
- [ ] `:` `:` `/`: the list selection moves down, down, up, over group rows
      too; a collapsed group stays closed; typing goes on on the canvas.
- [ ] `k` on a group: it opens / closes. On a part: that part at the cross;
      with a part selected: placed in the chain.

### K9f — A part's shortcut in its properties
*(user request 2026-11-19, before K10)*

- The properties dialog of a part that is in the component list has a last
  tab, **Keyboard** (seldom used: kept out of the way of the common fields),
  with a **Shortcut** field: the letter that places that **kind** of part
  at the cross (shortcuts are per kind, as in Manage Components), its default
  shown beside it. Typing a letter replaces it (a capital becomes its lowercase
  letter: capitals are switch keys); emptying it leaves the part without one.
- Only **a–z**; **h w k** are reserved (pan, wire, component-list part) and
  refused with the reason, as in Manage Components (one shared check,
  `ComponentSelector::keyRefusal`).
- A letter **another part already has**: a dialog *“r” is already the shortcut
  of Resistor*, explaining that the letter will place this part and Resistor
  will have none, with **Replace** and **Cancel** (default). Replace saves both (the other part's letter saved empty, so its
  default does not come back after a restart).
- Saved in the user's component settings (`compList.ini`, as Manage
  Components). The dialog is reused, so the field is re-read when it shows.
- CLI `kbd partkey <part> [<letter>|none] [--replace]` drives the same code;
  on a headless server it changes the shortcuts in memory only (a headless
  server shares the user's settings).

Validate
- [ ] Open a capacitor's properties, Keyboard tab: Shortcut c, default c.
- [ ] Type r: the dialog names Resistor; Cancel keeps c; Replace gives r to
      the capacitor and leaves the resistor without one.
- [ ] h, w, k or a digit: refused with the reason.
- [ ] Restart: the changes are kept.

### K9g — Toolbar keys, shown in the tooltips

Every toolbar button with a key shows it in its tooltip, written as the platform
writes it (D22): `Settings (⌘,)` on macOS, `Settings (Ctrl+,)` on Windows and Linux.

| Button | Key (macOS) | Key (Windows, Linux) | Read by |
|---|---|---|---|
| Settings | ⌘, | Ctrl+, | window shortcut |
| New / Open / Save / Save As | ⌘N ⌘O ⌘S ⇧⌘S | Ctrl+N Ctrl+O Ctrl+S Ctrl+Shift+S | the canvas when it has the focus (existing); anywhere else the key filter (`CircuitWidget::fileKey`, 2026-11-25) |
| Export as image | ⌘P | Ctrl+P | window shortcut |
| Undo / Redo | ⌘Z ⇧⌘Z | Ctrl+Z Ctrl+Shift+Z | the canvas (existing) |
| Zoom to fit | ⌥⌘0 | Ctrl+Alt+0 | the canvas (zoomKey) |
| Zoom to selected | ⌃⌘0 | Ctrl+Meta+0 | the canvas (zoomKey) |
| Zoom to scale 1 | ⌘0 | Ctrl+0 | the canvas (existing) |
| Start / stop | F5 | F5 | window shortcut |
| Pause / resume | F6 | F6 | window shortcut |
| Current flow | F7 | F7 | window shortcut |
| Capacitor charge, inductor current | F8 | F8 | window shortcut |
| Part states | F9 | F9 | window shortcut |
| Instruments | F10 | F10 | window shortcut |
| Transient noise | F12 | F12 | window shortcut |

- Undo / Redo stay read by the canvas (a text field undoes its own text), and the code
  editor keeps its own ⌘Z (window shortcuts would take it there too). The zoom keys are
  read by the canvas because ⌥ changes the character of 0 on macOS: 0 is also matched by
  its physical key, as ⌘0 already was (AZERTY: the à/0 key, without Shift).
- **⌘N, ⌘O, ⌘S and ⇧⌘S work wherever the focus is** (user, 2026-11-25). Before, only the
  canvas read them: after a click on Messages, a speed typed in the toolbar, a start of
  the simulation or in the Description tab they did nothing. Where the canvas has no
  focus, the key filter of the simulation keys acts (`CircuitWidget::fileKey`, in
  `eventFilter`), judged by the **focused widget**: a key press goes up from it to its
  parents, which each ask the filter, so the receiver alone would not tell. So: from a
  panel of the main window, with no widget focused, and from another window of the app
  (a part's properties dialog). Not in the code editor, which reads them in all of it for
  its own file (`EditorWidget::keyPressEvent`: from its output pane or its tab bar as from
  a document, and in every window that has the editor as parent, its Find dialog); not
  while a popup or a modal dialog has the keyboard. A save takes a description still being
  typed in the Description tab (the edit goes on). In another window (a part's properties
  dialog, Settings), the field being edited loses the focus and gets it back first, so its
  text is taken as when that window is left: a label in a part's properties is saved by ⌘S,
  and kept when ⌘N asks whether to save.
  `tests/regress/test_file_keys_anywhere.py` (headless; the editor's output pane included)
  and `test_props_dialog_save_quit.py` (offscreen: a headless server closes the dialog).
- **Quitting with the caret in a field of an open properties dialog crashed**, found
  with that test: hiding the dialog took the focus from the field, which applied it to
  its part while the part's properties were being deleted. The dialog is now detached
  from its part first (`PropDialog::detach`, from `CompBase::~CompBase`). The test ends
  with `cli quit`: exit code 0.
- Noise is F12, not F11 as proposed: macOS uses F11 for Show Desktop.
- On a Mac keyboard the F keys need fn, unless *Use F1, F2, etc. keys as standard
  function keys* is on (System Settings > Keyboard).
- The tooltip follows the button's text: `Stop Simulation (F5)` while running.
- The same notation fix applies to the context menus (component, canvas, code
  editor) and the code editor's toolbar, which wrote `Ctrl+C` on macOS: they
  now show ⌘C (`KeyText::native`).
- CLI: `ui toolbar` lists the buttons with their tooltip and key; `ui type F5`,
  `ui type cmd+,` go through Qt's shortcut map as a real key does.
  `tests/kbd/test_toolbar_keys.py`.

Validate
- [ ] Hover each toolbar button: the tooltip ends with its key, ⌘ ⌥ ⌃ ⇧ symbols.
- [ ] fn+F5 starts, fn+F6 pauses / resumes, fn+F5 stops; fn+F7 … F10, fn+F12 toggle.
- [ ] ⌘, opens Settings, ⌘P the export dialog (also with the code editor focused).
- [ ] ⌥⌘à/0 zooms to fit, ⌃⌘à/0 to the selection, ⌘à/0 to 100 %.
- [ ] ⌘S saves, ⌘N and ⌘O act, with the focus in Messages, the speed box, the Description
  tab and a part's properties dialog; in the code editor they act on the file being edited.
- [ ] Right click a part: Copy ⌘C, Rotate CW ⌘R … (no "Ctrl").

### K11 — Selection rectangle with Shift+arrows

The keyboard's rubber band: as a mouse drag on empty canvas selects the parts it
touches, Shift+arrows from the cross draw a rectangle.

- Nothing selected, Shift+arrows (8 px steps) or Cmd+Shift+arrows (64 px) start a
  rectangle at the cross; the cross is its moving corner, shown, and the outline is
  dashed in the highlight colour.
- While it grows or shrinks, every part it touches is selected (the part's shape,
  as the mouse); parts only — wires and dots are not, and follow when the parts move.
- Releasing Shift ends it, the parts staying selected (the cross hides, as with any
  selection; the arrows then move them). Any other key ends it the same way and then
  acts; a click too. Esc cancels it: nothing selected, the cross at the corner.
- With a part selected, Shift+arrows move the selection as the arrows do (no rectangle).
- To free Shift+arrows (D23) the cross's steps changed: arrows 8 px, Cmd+arrows 64 px
  (also while drawing a wire, K9b). Moving a selection, a dot or a corner follows
  the same steps (arrows 8 px, Cmd+arrows 64 px).
- CLI: `ui type shift+Right ... shift` (`shift` alone: Shift released); `kbd state`
  gives `rect` [x, y, w, h] while one is drawn. `tests/kbd/test_rect_select.py`.

Validate
- [ ] Nothing selected: arrows move the cross 8 px, Cmd+arrows 64 px.
- [ ] Hold Shift, arrows: a dashed rectangle grows from the cross; parts turn selected as
      it reaches them, and unselected when it shrinks off them.
- [ ] Release Shift: they stay selected; arrows move them all, wires following.
- [ ] Esc while holding Shift: nothing selected.

### K12 — Several wires and wire colours
*(user request 2026-11-19; own spec: [wire-colors.md](wire-colors.md))*

- Network mode marks wires: Enter the current one, `*` the walk's net; Cmd+click
  toggles a wire, Cmd+double-click marks its net. Space opens the colours dialog
  (line colour, band colour, opacity, width) for the marked wires; Backspace deletes
  them. The marks go when network mode ends. A mouse Cmd+click no longer ends
  network mode on the press (`KeyboardController::mousePressed( cmdClick )`).
- Tests: `tests/kbd/test_wire_colors.py`.

### K10 — Shortcut list in Settings
*(epic: "listing shortcuts")*

- Tab **Shortcuts** in `AppDialog` (Settings), the last one: read-only,
  scrollable table, one shaded heading row per category (File, Edit, View,
  Simulation, Selection, Moving, Pins & wiring, Wires, Parts, Part letters,
  Switches). Columns: Action (wrapped), Keys, When (the registry's context).
- Keys as the platform writes them (D22): ⌘ ⇧ ⌥ ⌃ on macOS (`⇧⌘Z`; a word key
  gets a space: `⇧ Tab`, `⌥ its letter`), Ctrl+ Shift+ Alt+ Meta+ elsewhere.
  The registry keeps the macOS words (`Cmd+Shift+Z`); `Shortcuts::shown`
  converts them, modifier by modifier, through Qt's native text.
- Built from the `Shortcuts` registry + the part letters in effect (defaults and
  user changes), category *Part letters*, sorted a–z — nothing hand-typed
  (`Shortcuts::table`, also `kbd shortcuts --table`). Read again each time the
  dialog shows, so a letter changed in a part's properties appears.
- Text at the top: click on the canvas first; a part's letter is set in its
  properties, Keyboard tab; capital letters are kept for the `Key` of switches
  and push buttons (Shift+letter).
- A `QTableWidget`, not a tree: a tree view does not wrap long actions (its row
  heights ignore the column width); the rows are sized again when a column is.
- CLI: `view settings shortcuts` opens it; `tests/kbd/test_K10_list.py`.

Validate
- [ ] Every key of §4.2 and every Cmd shortcut appears once, in its category.
- [ ] Change a part shortcut (its properties, Keyboard tab) → the list shows it next time.
- [ ] Scrolls on a small window; long actions wrap; readable in dark and light theme.

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
  
- **D6** `-` is numeric; wiring is `_` (replaced by `w`, D20).

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
  selection (arrow moves, mouse drags and the rotation alignment shift move
  it along: `KeyboardController::selectionMoved`, which never scrolls the
  view — scrolling after the hidden cross made a mouse drag run away — and
  re-reads a single part's position instead of adding 4 px drag steps, which
  drifted on the 8 px cross grid); Esc re-reads the part's position before unselecting, so the
  cross is shown where the part is even when it moved without the selection
  (a library drop's ghost placement).
  
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
  §4.3 pairing), and pinFrom is that outgoing pin. After `w` wires a pin of
  the selected part, that part's selected pin becomes the pin **most
  opposite** to the wired one (D16's rule), so the next Enter or placement
  continues out the other side — and pinFrom / pinTo are **cleared**: a made
  wire's pins are no longer the wiring pair, so nothing stays highlighted.
  
- **D19** (K4 rev) Rotating a selected part (`Cmd+R`/`Cmd+Shift+R`) with an
  outgoing pin (pinFrom) on **another** part also moves it: its pin closest to
  the outgoing pin is brought onto that pin's row or column (the smaller
  offset is eliminated; the shape is irrelevant — any pin can land off the
  line), so a straight wire stays possible. pinFrom on the rotated part
  itself, or none: the plain rotation. The rotation and the move are one
  undo step (`Circuit::joinUndoSteps`), wires following.
  
- **D20** (review, 2026-11-19) The wire key is **`w`**, not `_` (Shift on most
  layouts, and an awkward reach in a chain `r 1 k Enter r 2 k w`). The wave
  generator moves from `w` to **`n`**. `w`, like `h`, is reserved: Manage
  Components refuses it as a part shortcut, and it is checked before the part
  letters. During a value edit `w` reverts the edit, then wires.
- **D14** (K1 rev) `h` starts pan mode (pointer: cross with arrows; arrows pan;
  any other key, a selection or a mouse press ends it). `h` is not a part shortcut.
  
- **D15** (K1 rev) `Cmd+=`/`Cmd++` zoom in, `Cmd+-` out, `Cmd+0` 100 %, cross kept
  in place.
  
- **D10** Mac AZERTY number row: while a single part with a numeric value is
  selected (or in EditValue), the number-row keys count as digits with or without
  Shift, matched by physical key (macOS `nativeVirtualKey` = `kVK_ANSI_0…9`, the
  number row on every layout). Their unshifted characters (`& é " ' ( § è ! ç à`)
  are used by no other shortcut. *Revised by D21: "with Shift" only where Shift
  types the digit.*
- **D21** (2026-11-19) **Other layouts than AZERTY.** Every command matches the
  character typed (`<` `>` `=` `+` `:` `/` `-` `.` `,` letters), so it follows the
  user's layout: `<` is Shift+`,` on US QWERTY, its own key on QWERTZ and AZERTY;
  `=` is Shift+0 on QWERTZ; `/` is Shift+7 on QWERTZ, Shift+`:` on AZERTY. Cmd
  combinations use Qt's key code, which macOS derives from the character with
  Shift (`Cmd+>` = Cmd+Shift+`.` on US). Digits: a digit character is a digit on
  any layout; the number row also counts **by position only without Shift**
  (AZERTY's `& é " …`). D10's "with Shift" stole the shifted symbols of QWERTY /
  QWERTZ: with a resistor selected, QWERTZ `=` (Shift+0) started typing "0"
  instead of network mode, and `/` (Shift+7) typed "7". The position comes from
  `PhysicalKeys::numberRowDigit` (macOS virtual key codes; Windows scan codes
  0x02–0x0B and X11 key codes 10–19 added, untested), also used by Cmd+0. CLI
  `ui type --layout us|de|fr` types as macOS sends those layouts;
  `tests/kbd/test_layouts.py` runs the commands on all three.
  
  

- **D22** (user, 2026-11-19) Shortcut texts in the GUI follow the platform: Qt's
  portable form in the code ("Ctrl+N", Ctrl = ⌘ on macOS), shown with
  `QKeySequence::NativeText` (⌘N on macOS, Ctrl+N elsewhere) through
  `KeyText::native` / `KeyText::tipWithKey` (`src/gui/keytext.h`). No hand-written
  "Ctrl+" in a visible text.

- **D23** (user, 2026-11-19) The cross, a selection, a dot or a corner move 8 px with
  the arrows and 64 px with Cmd+arrows; Shift+arrows (Cmd+Shift: 64 px steps) draw a selection rectangle from it
  (K11). Pan mode stays on `h`.

# 7. Next

