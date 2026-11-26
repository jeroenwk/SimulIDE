# Parts pane, BOM export and locked parts

Status: built (2026-11-26) on `feature/parts-pane`; ⌥-hover lock mark, dashed outline,
locked switch and the pane's look checked by the user (2026-11-26). Tests
`tests/kbd/test_parts_pane.py` (45 checks) and `tests/kbd/test_lock.py` (35 checks) pass.

Built as specified, with these differences:
- The pane is a tab only (no split).
- Ground is under Sources in the component list, so the Sources filter hides it (§5 listed
  it with the connectors).
- Other values (§3.2) is not computed for subcircuits and MCUs: building a reference
  instance of them loads files and touches global state. A part with a Model is compared
  with a fresh part of the same model, so the model's own parameters are not listed.
  Properties that differ between two fresh parts (a DS18B20's random ROM) are left out.
- CLI `comp move / rotate / flip / remove` refuse a locked part; `select` selects it.
- A click on a locked part counts as a click on empty canvas (`CircuitView::hitItem`): the
  keyboard cross goes there, as anywhere else (user, 2026-11-26); its pins and controls
  still take the click. ⇧⌘-drag (copy by drag) does not grab it either.
- No hand cursor over a locked part (user, 2026-11-26): the part, its labels and child items
  (a Text's text) lose their cursors while locked, so the canvas's own shows; pins and
  controls keep theirs. CLI `ui mouse` replies `cursor_shape` to test it.
- A part selected by the keyboard (Tab, Shift+Tab, Space's nearest part) or from the Parts
  pane (a row clicked, `view parts --show`) gets a highlight square that closes in on it
  (from 17 px out to 3 px) and fades in 0.45 s, so the eye finds it (user, 2026-11-26); the
  mouse does not, and a mouse press ends a flash. Screen only (`Circuit::flashParts`, drawn
  in `drawForeground`). An app setting, on by default: Settings → Circuit "Flash a part
  selected by the keyboard or the Parts pane", ini `Circuit/flashSelect`, CLI `settings set
  flashSelect`. CLI `kbd state` lists `flashing`; `tests/kbd/test_flash.py` (10 checks).
- CLI `view parts --filter default|all|…` sets the pane's toggles (tests start from them,
  since the toggles are kept in the user's settings).
- The CSV per-part export has the columns Label, Type, Model, Value, Other values, Tab and
  the BOM fields.

User request (2026-11-25):

> Parts pane, listing each part with: part type (as listed in the left pane),
> label, model, primary value, other configured values as summary, tab order in
> canvas. Button to export a BOM list to a file. Each can be sorted (default
> sorting as common for BOM lists). Search and filter options. Graphical parts,
> meters and sources are filtered out by default. Selecting a row(s) selects
> those parts on the canvas; selection of a part in the canvas also selects it
> in the parts list.

Follow-up request (2026-11-25), §10:

> The possibility to 'Lock' and 'Unlock' an item(s) on the sim canvas.
> Keyboard shortcuts (option-L / shift-option-L) plus new action in the context
> menu. Locked items are excluded from the TAB selection and from selection with
> the mouse within the canvas. Locked items have a lock symbol in the part list
> pane. Unlocking a part(s) can be done from context item in the part list or by
> clicking the lock symbol. Locked items can be selected on the canvas by
> selecting the corresponding part(s) in the part list.

Points marked **(proposal)** are not in the request; they are choices made here
to make it buildable and can be changed at review.

## 1. Where it lives

- A **Parts** tab in the bottom tabs, after Scripts (`CircuitWidget::m_bottomTabs`),
  or a split beside them, like the Key frames pane (`KeyFramePane`). Class
  `PartsPane` (`src/gui/circuitwidget/partspane.{h,cpp}`).
- It follows the current circuit (`follow()` from `CircuitWidget::clear()` and
  after a load, as `TriggerPane`).
- Only the BOM fields (§3.1) are edited in the table; everything else is
  read-only. A double-click on a read-only cell opens that part's properties
  **(proposal)**.

## 2. Which parts

- One row per **top-level part**: every `Component` in `Circuit::compList()`
  that is not a `Node` (junction) and has no parent item. Parts inside a
  subcircuit are not listed: **a subcircuit is always one row** (decided
  2026-11-25), in the pane and in the BOM.
- Instruments hidden by Show Instruments (`InstrumentView`) are still parts:
  they are listed (under the Meters filter) and greyed.
- The table refreshes after every edit (`Circuit::saveChanges`), load, undo /
  redo and paste, and when a part moves (the tab order changes). One rebuild per
  frame at most, so a drag doesn't rebuild it on every mouse move.

## 3. Columns

| Column | Content | Source |
|---|---|---|
| Type | the part's name as in the left pane (Resistor, Capacitor, NPN, 555, MT7833A…) | the `LibraryItem` name for the part's `itemType()`; for a subcircuit its name |
| Label | the part's label (R1, C3, Q2, IC1) | `idLabel()` |
| Model | the model, when the part has one: a `Model` property (BJT, MOSFET, diode, op amp…), a subcircuit's or MCU's package/device name; else empty | properties |
| Value | the primary value with its unit (10 kΩ, 100 nF, 5 V) | the property in `m_showProperty` (the one shown in the value label), formatted as on the canvas; empty when the part has none |
| Other values | **every changed setting** (decided 2026-11-25) in one clear line: see §3.2 | properties ≠ the part type's default, excluding the label, value, model and BOM fields |
| Tab | the part's position in the canvas Tab walk, 1-based; `—` for a locked part, which Tab skips (§10) | `KeyboardController::readingOrder()` (rows top to bottom, 16 px row band, then left to right), computed over all unlocked parts, filtered or not |
| Category | the left-pane group (Passive, Active, Sources, Meters, Graphical…) **(proposal)**, hidden by default, used by the filters | `LibraryItem::category()` |
| 🔒 | first column, narrow: a lock symbol on locked parts, empty otherwise; a click toggles it (§10.3) | `Component::isLocked()` |
| Manufacturer, MPN, Supplier, Supplier PN, Notes | the BOM fields, editable: §3.1 | stored with the part in the .sim1 |

- Columns can be shown / hidden and reordered (header context menu); the choice
  is kept in the settings (`Circuit/partsPaneColumns`).

### 3.1 BOM fields (editable)

The user wants the BOM to carry sourcing data (decided 2026-11-25: yes, with the
most common, standard fields). The set used by KiCad, Altium, Octopart and the
distributors' BOM tools:

| Field | Meaning | Example |
|---|---|---|
| Manufacturer | maker of the part | Texas Instruments |
| MPN | Manufacturer Part Number: the exact orderable part | NE555P |
| Supplier | where it is bought | Mouser |
| Supplier PN | the supplier's order number (SKU) | 595-NE555P |
| Notes | free text | "socketed", "do not fit" |

- **Stored per part in the .sim1** as ordinary component properties in a
  property group **BOM** on `Component` (keys `bomMfr`, `bomMpn`, `bomSupplier`,
  `bomSpn`, `bomNotes`), empty by default, so empty ones are not written and
  older SimulIDE versions ignore them. The same group shows in the part's
  properties dialog.
- **Edited in the table**: double-click or Enter on a BOM cell. With several
  rows selected, the edit applies to all of them (fill one MPN into R1, R2,
  R10 at once). Each edit is one undo step (`beginUndoStep` /
  `addCompChange` / `endUndoStep`), undoable from the canvas too.
- Copy / paste of parts keeps them; Copy on a BOM cell copies its text.
- Search (§5) matches them, and they count in grouping (§7): two 10 kΩ
  resistors with different MPNs are two BOM lines.

### 3.2 Other values: the one-liner

- Every property whose value differs from the default of a freshly placed part
  of that type (compare with a reference instance from the library, built once
  per type and cached). Display-only properties count too, since they were set
  on purpose (e.g. Show State).
- Format: the property's caption as in the properties dialog, `: `, the value
  with its unit as there, joined with ` · `, in the dialog's order:
  `Tolerance: 5 % · Max voltage: 50 V · Show State: on`.
  Booleans as `on` / `off`, enums by their shown text.
- Elided at the cell's width; the tooltip shows the whole line. The CSV holds
  the whole line.

## 4. Sorting

- A click on a column header sorts on it; a second click reverses it.
  Shift+click adds a secondary key **(proposal)**.
- **Default sort, as in BOMs:** by the label's letter prefix, then by its number
  in natural order: C1, C2, C10, D1, IC1, Q1, R1, R2, R10. Labels without a
  number come after their prefix group; parts without a label at the end, by
  Type.
- The Value column sorts by the numeric value (100 Ω < 1 kΩ < 10 kΩ), not the
  text. Tab sorts numerically.
- The sort order is kept per session, not per circuit.

## 5. Search and filters

- A **search box** above the table: plain text, case-insensitive, matched
  against every visible column (so `10k`, `BC547`, `R1` or `Tolerance` all
  work). Value matching accepts the SI forms: `10k`, `10 kΩ` and `10000` find
  the same parts **(proposal)**.
- **Filter toggles** beside it (check buttons), each a category of the left
  pane:

  | Filter | Default | Parts |
  |---|---|---|
  | Graphical | **off** | Image, Text, Rectangle, Ellipse, Line (`isGraphical()`) |
  | Meters | **off** | Probe, Voltmeter, Ammeter, Frequency meter, Oscilloscope, Logic analyzer; also Tunnel and Bus (not bought parts) |
  | Sources | **off** | Fixed Volt, Clock, Wave Gen, Voltage / Current source, Noise, controlled sources, Battery, Rail |
  | Everything else | on | Sockets and headers shown by default (decided 2026-11-25): they are bought parts. Tunnels and buses are not: they go with the Meters toggle, off by default (decided 2026-11-26) |

  A "More…" menu lists every category with a check box for finer filtering
  **(proposal)**. The toggles are kept in the settings.
- A line under the table: `24 of 41 parts shown` (`n selected` when any).

## 6. Selection, both ways

- Selecting rows (click, Shift+click range, Cmd+click toggle, Cmd+A) selects
  exactly those parts on the canvas, as one selection change, and centres the
  view on them when they are off screen (`view show`'s behaviour) **(proposal)**.
- Selecting on the canvas (click, rubber band, Tab, Space, CLI `select`)
  selects the matching rows and scrolls the first into view. Parts that are
  filtered out stay selected on the canvas; the count line says
  `2 selected (1 hidden by filters)`.
- Both directions go through one guard flag, so a change made by the pane does
  not echo back into it (`Circuit::selectionChanged` → pane; pane → scene with
  signals blocked).
- With the focus in the table, Delete does nothing (the canvas keeps its own
  keys) **(proposal)**: no destructive edit from a list that can hide parts.

## 7. BOM export

- An **Export BOM…** button above the table opens a save dialog; default name
  `<circuit>_bom.csv` next to the circuit.
- **What is exported: the parts the filters let through** (the search box is
  ignored, so a search never trims a BOM by accident) **(proposal)**.
- **Grouped, as BOMs are:** one line per identical part (same Type, Model,
  Value, Other values and BOM fields), with the quantity and the labels, sorted
  as in §4 by the first label:

  ```
  Qty,Labels,Type,Model,Value,Other values,Manufacturer,MPN,Supplier,Supplier PN,Notes
  3,"R1, R2, R7",Resistor,,10 kΩ,,Yageo,CFR-25JB-52-10K,Mouser,603-CFR-25JB-52-10K,
  2,"C1, C4",Capacitor,,100 nF,Max voltage: 50 V,,,,,
  1,Q1,NPN,BC547,,,onsemi,BC547BTA,,,
  ```

- Formats: **CSV** (UTF-8 with BOM so Excel reads Ω and µ, comma-separated,
  RFC 4180 quoting) and **Markdown** table, chosen by the file extension in the
  dialog **(proposal)**. A "Per part (not grouped)" check box exports the table
  as shown instead, with the Tab column.
- The circuit name and date go in a title line of the Markdown only; the CSV
  starts with its column header, so it imports cleanly.
- The result is written to Messages: `BOM: 14 lines, 31 parts → path`.

## 8. CLI

As every GUI feature here, the pane is driven and checked from the CLI
(`src/cli/cli_parts.cpp`, documented in `docs/CLI.md`):

- `parts set <comp>[,<comp>…] mfr|mpn|supplier|spn|notes=TEXT …`: sets BOM
  fields, one undo step (the same as `set <comp> bomMpn=…`, which works too).
- `parts list [--all] [--filter graphical,meters,sources] [--search TEXT] [--sort COL[,COL]] [--json]`:
  the table's rows, by default with the pane's default filters and sort.
- `parts bom [--out FILE] [--format csv|md] [--per-part] [--all]`: the export;
  without `--out`, the CSV on stdout.
- `lock <comp>[,<comp>…]` / `unlock <comp>[,<comp>…]` (`--all` for `unlock`):
  §10, one undo step; `parts list` has a `locked` field and `--locked` to list
  only those.
- `view parts [--search TEXT]`: shows the Parts tab (shared GUI session).
- `select` already selects parts; `parts list --selected` reads back what the
  pane shows as selected, so the two-way sync can be tested.

## 9. Tests

`tests/kbd/test_parts_pane.py` (GUI through the CLI), on a circuit with
resistors R1, R2, R10 (same value), a BJT with a model, a subcircuit, a scope,
a source, a rectangle and a tunnel:

1. Default view: graphical, meters, sources and the tunnel hidden, a header shown; order
   R1, R2, R10 (natural), not R1, R10, R2; the subcircuit is one row.
2. Columns: Type matches the left pane, Value as on the canvas, every changed
   setting appears in Other values in the dialog's order, default ones don't.
2b. BOM fields: edit an MPN in the table on three selected rows (one undo
   step, undone from the canvas), save and reopen the .sim1 (kept), a part
   without them writes no `bom*` attribute; `parts set`.
3. Tab column equals the order Tab walks on the canvas; moving a part updates it.
4. Sort by Value (numeric), by Tab, reversed.
5. Search `10k` and `BC547`; each filter toggle on and off.
6. Selection: rows → canvas, canvas (click, rubber band, Tab) → rows, no echo
   loop, a filtered-out selected part is counted.
7. BOM: CSV grouped (`3,"R1, R2, R10",…`), filtered parts left out, `--per-part`,
   Markdown; Ω survives a round trip through a CSV reader.
8. Live update: add, delete, undo, paste, load another circuit.
9. Locking (`tests/kbd/test_lock.py`): ⇧⌘K on two selected parts locks them
   (one undo step, undone), the 🔒 shows in the pane; a click on a locked
   part selects the part under it or starts a rubber band; a rubber band
   around it leaves it out; Tab, Space and ⌘A skip it; selecting its row
   selects it on the canvas, where it can't be dragged, moved with the
   arrows, rotated, flipped or deleted (Messages says so); ⌥⇧⌘K, the row menu and a click on the 🔒 unlock; the lock survives
   save / reopen; a locked switch still toggles while the simulation runs.

## 10. Locking parts

The use: fixing parts that are done (a background rectangle, a placed board, a
finished block) so clicks, rubber bands and Tab no longer catch them while the
rest is edited.

### 10.1 Lock and unlock

| Action | Keys (macOS) | Where |
|---|---|---|
| Lock | ⇧⌘K | the canvas with a selection; a part's context menu (**Lock**, with the key shown); the Parts pane row menu |
| Unlock | ⌥⇧⌘K | the canvas with locked parts selected (selected from the pane); the Parts pane row menu (**Unlock**); a click on the 🔒 |

- They act on all selected parts. Keys decided 2026-11-25 (the request first
  said ⌥L / ⇧⌥L; ⌘L and ⇧⌘L are the flips): ⇧⌘K locks, ⌥⇧⌘K unlocks, the
  same "⌥ = the other variant" pattern as the stacking keys. ⌘K (save a key
  frame) is unchanged: Shift tells them apart. With ⌥ in the combination the
  K is matched on the physical key (`nativeVirtualKey`), since Option changes
  the typed character on macOS, as the stacking-order keys in
  `specs/gfx-arrange.md`. Both go in the registry (`shortcuts.cpp`, group
  Edit), so Settings → Shortcuts lists them.
- In the pane, the row menu shows **Lock** and / or **Unlock** depending on
  the selected rows, plus **Unlock all** **(proposal)**. A click on the empty
  🔒 cell of an unlocked part locks it **(proposal)**, so the column is a
  toggle both ways.
- Each lock or unlock is one undo step. The state is saved in the .sim1 as a
  `Locked` property of the part (written only when true), hidden from the
  properties dialog (it is set with the keys, the menu and the pane).
- Messages: `Locked R1, R2` / `Unlocked R1`. Nothing to do (all already
  locked): a line saying so.

### 10.2 What a locked part does on the canvas

- **Not selectable with the mouse:** a click passes through it to what is
  below (the part under it, a wire, or the empty canvas, where it starts a
  rubber band). A rubber band never selects it. Implementation: the part
  accepts no mouse buttons (`setAcceptedMouseButtons(Qt::NoButton)`) and the
  rubber-band result drops locked parts (`Circuit::selectionChanged`, unless the
  change comes from the pane).
- **Skipped by the keyboard:** Tab / Shift+Tab (`readingOrder()` leaves it out),
  Space-nearest (`selectNearest`) and ⌘A (Select All, decided 2026-11-25).
- **Not edited on the canvas while locked**, also when selected from the pane
  (decided 2026-11-25): no drag, no arrow-key move, no rotate, flip, cut or
  delete. A selection that includes locked parts acts on the others only, and
  Messages says `R1, R2 locked: not moved` (rotated, deleted…). Properties
  can still be opened and changed **(proposal)**: the lock is about the
  layout.
- **Still works in the simulation** **(proposal)**: switches, push buttons,
  potentiometers and other controls of a locked part are still operated by
  the mouse. The lock is about editing, not about the circuit.
- **Parts only** (decided 2026-11-25): wires are not lockable. Wires attached
  to a locked part stay selectable and editable; dragging a wire's corner never
  moves the locked part's pin.
- **Right-click** on a locked part: its context menu, with only **Unlock**,
  **Properties** and **Show in Parts pane** **(proposal)**, so a locked part
  can be freed without opening the pane.
- **Shown** (decided 2026-11-25): no mark on the canvas normally; while the
  pointer is over it with ⌥ held, a small 🔒 at its top-right corner, drawn like the
  corner handles (not in exports or `capture circuit`). When selected from the
  pane, the selection outline is dashed.
- **Copy / paste:** copies are pasted unlocked, so they can be placed
  **(proposal)**.

### 10.3 Locked parts in the Parts pane

- The 🔒 column (§3) shows the lock; locked rows are listed normally, sorted and
  filtered like the others, and the filters get a **Locked** toggle (on by
  default) **(proposal)**.
- Selecting a locked part's row selects it on the canvas (the only way, apart
  from CLI `select`). The canvas → pane sync (§6) is unchanged.
- A click on the 🔒 unlocks that part (all selected rows when the clicked row
  is one of them), one undo step.

## 11. Decisions (user, 2026-11-25)

1. Sockets and headers are **not** filtered out by default (§5); tunnels and buses are,
   under the Meters toggle (2026-11-26).
2. A subcircuit is **one row** (§2).
3. Other values: **every changed setting**, in one clear line (§3.2).
4. BOM sourcing fields: **yes**, the common standard set Manufacturer, MPN,
   Supplier, Supplier PN, Notes, stored in the .sim1 and editable in the
   pane (§3.1).
5. A locked part selected from the pane can't be moved, rotated, flipped,
   cut or deleted until unlocked (§10.2).
6. ⌘A leaves locked parts out (§10.2).
7. Only parts can be locked, not wires (§10.2).
8. The lock mark on the canvas shows only on ⌥-hover (§10.2).
9. Keys: ⇧⌘K Lock, ⌥⇧⌘K Unlock (§10.1).

## 12. Tab order (user request, 2026-11-26)

> When multiple parts are selected, an option to make their tab numbers follow the order in
> which I selected them (context menu, ⇧⌘T); move the selected parts to the front or back of
> the tab order (context menu, ⌥⌘T / ⇧⌥⌘T); edit a part's tab index in the parts list:
> entering an existing index shifts the others.

Built 2026-11-26 (`taborder.{h,cpp}`, `tests/kbd/test_tab_order.py`, 20 checks):

- **Model.** A part may carry a tab index (`tabIndex="N"` in the .sim1, hidden, written only
  when set). The walk (`TabOrder::walk()`): parts with an index in index order, then the
  others in reading order. No index anywhere: the old walk by position. Locked and hidden
  parts are not in it (they keep their index and come back to their place).
- **Every change** writes the places 1..n of the whole walk as indices, one undo step, and a
  line in Messages (`Tab order: R3, R1 → 1–2`). New parts and pasted copies have no index:
  they come after the ordered parts. **Reset (by position)** clears the indices.
- **As selected** (⇧⌘T, context menu Tab order → As selected): the selected parts become one
  block in the order they were selected, where the first of them (in the walk) was. The
  canvas keeps the selection order (`Circuit::selectionOrder()`): clicks and ⌘-clicks one
  by one, the pane's rows likewise; parts selected together (a rubber band) in reading order.
  Fewer than two parts: nothing changes.
- **To front / To back** (⌥⌘T / ⇧⌥⌘T, context menu): the selected parts, in their current
  order among themselves, to the start or the end of the walk. With ⌥ the physical T decides.
- **Parts pane:** the Tab cell is editable (double-click or Enter, a number): the part moves
  to that place and the others shift; with several rows selected they move there as a block.
  The row menu has the same Tab order submenu. A locked or hidden part's — cell is not
  editable.
- **CLI:** `tab order [list|selected|front|back|set N|reset] [comp,…]` (reply: the walk with
  places and indices, the selection order).
