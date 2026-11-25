# Space selects the nearest; arranging and resizing graphical shapes

Status: implemented (2026-11-25), branch `feature/gfx-arrange`, to validate by
hand. Tests: `tests/kbd/test_gfx_arrange.py` (36 checks).

User request (2026-11-25):

1. With nothing selected, Space selects the nearest part or wire.
2. Graphical parts: a context menu and keys for the stacking order (Send
   Backward ⌥⇧⌘B, Send to Back ⇧⌘B, Bring Forward ⌥⇧⌘F, Bring to Front ⇧⌘F)
   that set the part's Z value; corner handles on a single selected part to
   resize it, highlighted with `=` (nearest to the cross), Tab / Shift+Tab or
   a click, moved with the arrows or a drag.

"Graphical shapes" here are the parts built on `Shape`: **Rectangle, Ellipse,
Line, Image**. They have a size (`H_size`, `V_size`) and a `Z_Value`. Text is
left out: its size comes from its text and it has no Z value.

## 1. Space with nothing selected

- Space, nothing selected, the canvas focused: the part or the wire nearest to
  the cross (`KeyboardController::selectNearest`).
- A part counts by its outline (`sceneBoundingRect`, 0 when the cross is
  inside it). Inside a shape the distance to its edge is used instead, so the
  parts drawn on a background rectangle come first; the shape itself is taken
  near its edge. Among parts the cross is inside, the smallest.
- A wire counts by its nearest segment (`NetworkWalk::nearestWire`; hidden
  instrument leads are skipped). A wire strictly nearer than every part:
  network mode on that wire (as Cmd+click on it, K9d): highlighted, Tab walks
  on, Space opens its colours.
- Nothing in the circuit: a message. Space with a selection is unchanged
  (properties, wire colours). Space never reaches the simulation while the
  canvas has the focus (sim-control D1), so it does not clash with play / pause.

## 2. Stacking order

| Menu entry | Keys (macOS) | Effect |
|---|---|---|
| Bring to Front | ⇧⌘F | above every other shape |
| Bring Forward | ⌥⇧⌘F | one step up |
| Send Backward | ⌥⇧⌘B | one step down |
| Send to Back | ⇧⌘B | below every other shape |

- The entries are at the top of a shape's right-click menu (with the keys
  shown) and act on the selected shapes (the clicked one alone if it was not
  selected). The keys act on the selected shapes, also with a corner
  highlighted. The physical F and B keys are matched (`nativeVirtualKey`), since
  Option changes the character on macOS.
- **The order is among the shapes only** (`ZOrder`, `zorder.{h,cpp}`). The
  stack is every top-level shape in the scene's stacking order (Z, then
  insertion order, as drawn).
- **One step** passes the nearest shape on that side that overlaps it (bounding
  rectangles): a step past a shape it does not touch would change nothing
  visible. When none overlaps, it passes the next one. Several selected shapes
  move one by one, the one nearest the end first.
- **Z values**: the values the shapes already use are kept as the slots, sorted
  and made strictly increasing by pushing ties down (four shapes at the default
  −1 become −4, −3, −2, −1), then given out in the new order. So shapes stay in
  their band: behind the parts (Z 0) and wires (100) when they were all behind,
  and a shape a user set above the parts keeps that slot for whichever shape is
  now on top. Only changed Z values are written.
- One undo step (`Z_Value` of each changed shape). Nothing to do (already in
  front / at the back, no shape selected): a line in Messages.

## 3. Corner handles

- **Shown** when exactly one shape is selected: a small square (7 screen px,
  whatever the zoom) on each corner, drawn by the keyboard controller in the
  canvas foreground — not in exports or `capture circuit`. Unhighlighted: canvas
  fill, outline colour, 75 % opacity. Highlighted: filled in the highlight
  colour (as the cross and the walked corner of a wire).
- A **Line** has two handles, its ends (`handleCorners()`: top right and bottom
  left of its box; the line is now drawn exactly between those corners, odd
  sizes too). Rectangle, Ellipse and Image have four.
- **Highlighting a corner**
  - `=` with the shape selected: the corner nearest to the cross. While a part is
    selected the cross is parked on it, so "the cross" is where it was before the
    shape was selected by a key (Tab, Space), or the point of the last mouse press
    (a click that selected it); ties go to the first clockwise from the top left.
  - Tab / Shift+Tab: next / previous corner, clockwise on the canvas (rotation and
    flips included), wrapping.
  - A left press on a handle highlights that corner (and starts a drag).
  - `=` or Esc: no corner highlighted, the shape stays selected. Any other key
    leaves the corner too, then acts as usual (Cmd+Z, Backspace, a part letter…);
    `h` (pan) and the arrange keys keep it. Another selection drops it.
- **Moving a corner**: arrows 8 px, Cmd+arrows 64 px; a drag of the handle moves it
  in 4 px steps (`toGrid` of the pointer, as a part's drag), keeping its offset
  from the grid. The opposite corner stays where it is: `H_size`, `V_size` and
  `Pos` change (`Shape::setCornerScene`, in the part's own frame, so rotated and
  flipped shapes resize along their own sides).
  - The size stops at 1 px. A **Line** flips instead (`hflip` / `vflip`) when an
    end is moved past the other one, so its ends can go anywhere.
  - A burst of arrow presses on one shape is one undo step (as arrow moves); a drag
    is one undo step (`Shape::saveCornerUndo`: Pos, sizes, flips).
- CLI: `kbd state` has `shape` while one shape is selected: `uid`, `handles`
  (corner index and scene position, clockwise from the top left), `corner` (the
  highlighted one), `size`, `pos`, `flip`, `z`.

## 4. Validate (by hand)

- [ ] Nothing selected, cross near a wire, Space: the wire highlighted; Tab walks.
- [ ] Cross near a part, Space: that part; inside a big background rectangle, near
      a part: the part; near the rectangle's edge: the rectangle.
- [ ] Three overlapping rectangles: right-click → Bring to Front / Send to Back /
      Forward / Backward, and the four keys, do what they say; Cmd+Z undoes each.
- [ ] A rectangle behind parts stays behind them after Bring to Front.
- [ ] Select a rectangle: four subtle handles. `=`: the corner nearest where the
      cross was; Tab / Shift+Tab go round; arrows resize, the opposite corner stays.
- [ ] Click a handle: it lights up; drag it: the shape resizes; Cmd+Z: one step.
- [ ] A rotated rectangle and an image resize along their own sides.
- [ ] A Line: two handles; drag one end past the other: the line flips, follows.
- [ ] Esc: the handle highlight goes, the shape stays selected; Esc again unselects.
