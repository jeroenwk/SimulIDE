# Border line type and colour of the graphical items

Status: implemented (2026-11-24), branch `feature/gfx-border-style`.

## Why

The border of a Rectangle, Ellipse, Image or Text was drawn with a hard-coded
black, solid pen, and a Line was always solid: only the width (`Border`) could
be set. A border that has to be seen on a dark canvas (black is not), a box
marked as optional or temporary with a dashed edge, or a group outlined in a
figure exported for the books could not be drawn.

## Behaviour

- Per item, saved in the circuit:
  - `Border_Style`, the line type of the border: `Solid`, `Dash`, `Dot`,
    `DashDot`, `DashDotDot` (Qt's pen styles, shown as Solid, Dashed, Dotted,
    Dash-Dot, Dash-Dot-Dot). Default `Solid`.
  - `Border_Color`, `#rrggbb`. Default `#000000`: black, as the border was
    always drawn, so circuits saved before look the same.
- Rectangle, Ellipse, Image and Text have both. A Line has `Border_Style`
  only: its colour is its `Color`.
- The dash unit is the pen width, as in Qt: the dashes and gaps grow with
  `Border`.
- `Border` 0: as before. Rectangle, Image and Text draw no border; an Ellipse
  keeps its 1 px line, now in the border's colour and line type.
- `Opacity` applies to the fill, not to the border (as before).
- Shown in PNG / SVG exports and `capture circuit`. The SVG carries the
  colour as `stroke` and the line type as `stroke-dasharray`, with round caps
  as on the canvas.

## Interfaces

- Properties of a Rectangle, Ellipse or Image: **Line Type** in Main, under
  Border; **Border Color** in Color, under Color. Of a Text: both in Main,
  Line Type under Border, Border Color under Background Color.
- CLI, as any enum and colour property: `comp set Rectangle-1
  Border_Style=dashed Border_Color=#ff0000` takes the value, its caption or
  its index; `comp get` lists the values. `Border_Color` on a Line is
  refused (unknown property).
- Undo: one step per change in the dialog or from the CLI. Copy / paste
  and save / reopen keep both.
- A value that is not one of the five (junk in the file) loads as Solid; a
  `Border_Color` that is not a colour keeps the one the item had.

## Implementation

- `BorderStyle` (`components/borderstyle.h`): the values and their captions,
  the saved name of a value, caption or index, the Qt pen style, and the pen
  (round caps and joins, as before).
- `Shape` owns both properties (a Line removes `Border_Color` in its
  constructor); `TextComponent` has its own, as it does not derive from
  `Shape`.
- `Ellipse::paint` no longer draws its 1 px line under a border: with a line
  type set, its dots showed in the gaps of the border.

## Decisions (2026-11-24, taken while implementing; to confirm in review)

- The default stays black, not the theme's line colour: circuits saved
  before must look the same, and a theme colour on a black canvas would
  change them in the dark themes only.
- A Line gets no `Border_Color`: its `Color` is the line's colour.
- In a dark theme the default border is still invisible; a border colour per
  item is the way out.

## Not done

- A border colour that follows the theme (as a Text's Font Color can), a
  custom dash pattern, a border per side, other caps or joins.

## Check

[gfx-border-style.png](gfx-border-style.png): the test circuit in the Light theme (in a dark
theme the black default borders of the last row are not visible, the reason to give a border a
colour).

`tests/circuits/border_styles.sim1`;
`tests/regress/test_gfx_border_style.py` (properties and defaults, the pixels
of each style and colour, the black default on a light canvas, save /
reopen, undo / redo, copy / paste, junk in the saved attributes, the SVG
export, the test circuit).
