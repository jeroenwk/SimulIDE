# Label layout: name and value on one line

Status: implemented on `feature/label-layout` (2026-11-10).

## Why

A part showed its name (id label) on one side and its value on the other, in two font
sizes (10 px and 8 px). With the animation details on (capacitor charge, inductor current:
`ChargeGauge`, drawn on the side opposite the value label), the part was surrounded by
text on both sides. Putting the name next to the value frees the other side for the gauge.

## Behaviour

- **Font**: the name uses the value's font size (8 px), for every part, in old circuits too
  (fonts are not saved).
- **Automatic placement** (new parts): the shown labels go on one line, name first, value
  second, separated by a space width, with the same baseline. The line is centred on the
  part, just outside its outline (its bounding rectangle plus its pins), with a 2 px gap.
- **Side**: the side where the part keeps its value label by default
  (`setValLabelPos` in its constructor; `Comp2Pin`: below). When the value is hidden, the
  name's default side (`setLabelPos`; most chips: above). A label whose default lies above
  or below the outline goes above or below, centred horizontally; left or right of it, the
  line goes there, centred vertically; inside the outline, above or below by its centre.
- **Rotation**: the line is in the part's own frame and turns with the part (labels already
  did). Flips mirror the side, as before.
- **Exceptions**: parts whose default labels are rotated (probe 45°, rail, KY-023, KY-040,
  the DIP resistor's value) keep their own placement.
- The layout is redone (deferred to the event loop, once per batch of changes) when a label
  text changes (rename, value change), when Show_id / Show_Val changes, and when the part
  sets new default positions (e.g. a chip after loading its package).

## Manual placement and old circuits

- Every part has a hidden, saved property `labelAuto`, declared after `idLabPos`,
  `labelrot`, `valLabPos` and `valLabRot`. Properties load in declaration order, so:
  - a circuit saved before this change has positions but no `labelAuto`: setting the saved
    positions switches the part to manual, and its labels stay where they were;
  - a newer file restores `labelAuto=true` after the positions, and the part is laid out again.
- Dragging or rotating a label by hand switches its part to manual.
- Old circuits are not converted. A later, general "tidy up circuit" feature will lay out
  whole circuits, labels included.

## Not covered

- The part-state pills (`StatusView`) are drawn above the part and can overlap a name line
  placed above.
- Parts with the exceptions above.

## Logging

`[labels]` lines: the side chosen, the line's position and the switch to manual or auto.
