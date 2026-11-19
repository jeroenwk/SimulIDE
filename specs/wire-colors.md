# Wire colours and selecting several wires

Status: implementing (2026-11-19), branch `feature/wire-colors`. Decisions by the
user on 2026-11-19 (W1–W4, §4).

Goal: mark wires on the schematic, as in a hand-annotated diagram: a **line colour**
(the wire itself) and/or a **band**, a wide translucent stripe behind the wire. Several wires
at once, chosen with the mouse or the keyboard; a whole net in one action.

Example uses: a net's identity (DSEN in purple), the high-current loop of a switcher
against the quiet return of its feedback divider (one ground net, two colours), the
segment of a net matching a suspect PCB trace, the branches already checked with
the continuity tester.

---

## 1. Model

- Colours belong to **one wire** (`Connector`, pin→pin or pin→dot), W1. A net is
  coloured by selecting all its wires (§2), which is one action.
- Three properties of `Connector`, saved in the `.sim1` only when set:
  - `color`: line colour, `#rrggbb`; empty = the theme's wire colour.
  - `bandColor`: band colour with opacity, `#aarrggbb`; empty = no band.
  - `bandWidth`: band width in px; empty = the default **6 px** (W4). Set per wire
    in the dialog (2–40 px).
- Undo: one step per change applied from the dialog or the CLI, whatever the number
  of wires. Copy / paste, undo of a delete and save / load keep them.

## 2. Selecting several wires

Network mode (K9) gets a **wire selection**: the *marked* wires. The walk's
current step stays the cursor that Tab moves.

| Action | Effect |
|---|---|
| `=` (K9 / K9c) | network mode, nothing marked; the current step acts as the selection |
| Enter (network mode) | mark / unmark the current wire (a dot or corner: a message) |
| `*` (network mode) | mark every wire of the walk's net, instrument leads left out (W5); all already marked: unmark them |
| Cmd+click a wire (K9d) | in network mode: mark / unmark it (the current wire is marked first, so the first Cmd+click on a second wire gives two); the walk moves to it, also onto another net. Outside: network mode on it, as K9d |
| Cmd+double-click a wire | mark every wire of its net, instrument leads left out (W5) |
| Tab / Shift+Tab | walk on, marks kept |
| Space (network mode) | wire colours dialog for the marked wires, or the current wire when none is marked |
| Backspace / Delete | delete the marked wires (one undo step), or the current wire |
| Esc, `=`, a click on the canvas, any other key | leave network mode; the marks go |
| Right click a wire → *Colours…* | the dialog for the marked wires if this wire is one of them, else this wire |
| Space with only wire segments selected (rubber band) | the dialog for those wires |

Drawing: marked wires are traced solid in `highlight` (as K9 traces the current
wire). While some are marked, the current step is drawn dashed, so Tab's position
stays visible.

The net is the walk's (K9): the wires reachable through dots, never through a
part (a tunnel is a part).

## 3. The dialog

`WireStyleDialog`, modal, titled "Wire colours" (with the count when there are several):

- **Line**: a colour swatch (opens the colour picker) and *None* (back to the theme).
- **Band**: a swatch (the picker shows its opacity), an **opacity** slider 0–100 %
  (default 50 %, kept in step with the picker's alpha), a **width** slider + spin box
  2–40 px (default 6), and *None* (no band).
- The wires show the change at once (preview); **OK** commits it as one undo step,
  **Cancel** / Esc restores them.
- Several wires with different values: the first wire's values are shown, and only
  the fields the user changes are applied to all.

## 4. Drawing

- Line: the custom colour replaces the theme colour, the voltage animation colour
  and an instrument's lead colour (the user chose it). A selected wire keeps the
  selection grey.
- Junction dot: the colour shared by at least two of its wires, else the theme's.
- Band: drawn by `Circuit::drawBackground` after the grid, below every part and wire,
  round caps and joins. All bands of the same colour and width are **one path
  stroked once**, so where segments or wires of a net overlap (corners, dots) the
  opacity does not add up. Shown in PNG / SVG exports and `capture circuit`.
- Hidden wires (instrument leads hidden, board mode) have no band.

## 5. CLI

- `wire style <wire ...> [--color C|none] [--band C|none] [--opacity 0..100]
  [--width W|default]`: sets them (one undo step); without an option prints them.
  C: `#rrggbb`, `#aarrggbb` or a colour name.
- `wire list` reports `color`, `bandColor`, `bandWidth` when set.
- `kbd state` → `network.marked`: the marked wire uids.
- `ui click x,y --cmd --double`: Cmd+double-click.
- Tests: `tests/kbd/test_wire_colors.py`.

## 6. Decisions (user, 2026-11-19)

- **W1** Colours per wire, with quick ways to select a whole net (`*`,
  Cmd+double-click), so both a whole net and one branch can be marked.
- **W2** Mouse: Cmd+click toggles wires (as on parts); Cmd+double-click adds the net.
- **W3** Keyboard: Enter marks in network mode, `*` the net, Space the dialog.
- **W4** The band has a default width, changed per wire in the dialog: 6 px, and a
  default opacity of 50 % (user, 2026-11-19; first 10 px / 35 %).
- **W5** Selecting a whole net (Cmd+double-click, `*`) leaves out the wires of
  instrument leads (scopes, logic analyzers, voltmeters, frequency meters, probes:
  `Connector::leadPin()`, through tunnels too) and every wire of a branch that only
  leads to instruments (two scope channels joined at a dot, then one wire to the net:
  that wire too). They can still be marked one by one with Cmd+click / Enter. A net
  made of leads only: all its wires.

## Validate

- [ ] Cmd+click a wire, Cmd+click two more: three traced; Space: the dialog says 3 wires.
- [ ] Line colour green, OK: the three wires green, their shared dots too. Cmd+Z: back.
- [ ] Band purple (opacity 50 %, width 6 by default): one even band, no darker spots at corners and dots.
- [ ] `=` on a part, Tab, Enter, Tab, Tab, Enter: two marked, the current one dashed.
- [ ] `*`: the whole net; `*` again: nothing marked.
- [ ] Cmd+double-click a wire: its net marked, a voltmeter's lead on it not.
- [ ] Save, reopen: colours and bands back. Export PNG / SVG: bands shown.
- [ ] Rubber band over some wires only, Space: the dialog for them.
- [ ] Right click a wire → Colours…
