/***************************************************************************
 *   Shortcut registry (specs/keyboard-input.md §3)                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "shortcuts.h"

const QList<Shortcut>& Shortcuts::all()
{
    static const QList<Shortcut> list = {
        // Existing shortcuts (Circuit::keyPressEvent)
        { "File", "New circuit",            "Cmd+N",       "" },
        { "File", "Open circuit",           "Cmd+O",       "" },
        { "File", "Save circuit",           "Cmd+S",       "" },
        { "File", "Save circuit as",        "Cmd+Shift+S", "" },
        { "Edit", "Undo",                   "Cmd+Z",       "" },
        { "Edit", "Redo",                   "Cmd+Shift+Z or Cmd+Y", "" },
        { "Edit", "Copy",                   "Cmd+C",       "Selection" },
        { "Edit", "Cut",                    "Cmd+X",       "Selection" },
        { "Edit", "Paste",                  "Cmd+V",       "" },
        { "Edit", "Delete",                 "Backspace or Delete", "Selection" },
        { "Edit", "Rotate clockwise",       "Cmd+R",       "Selection" },
        { "Edit", "Rotate counter-clockwise", "Cmd+Shift+R", "Selection" },
        { "Edit", "Flip horizontally",      "Cmd+L",       "Selection" },
        { "Edit", "Flip vertically",        "Cmd+Shift+L", "Selection" },
        { "Selection", "Select all",        "Cmd+A",       "" },
        { "Selection", "Cancel the wire being drawn, stop linking", "Esc", "" },
        { "Parts", "Add the part bound to a letter at the keyboard cursor. A part selected (or the cross still where "
                   "Enter left it, beyond a pin): the new part's first pin faces that pin, a big step beyond it, in "
                   "a straight line; the part is rotated to match", "its letter", "Set in Manage Components" },
        { "Parts", "Add the part bound to a letter, at the mouse", "Alt+its letter", "Set in Manage Components" },
        { "Parts", "Add the part selected in the component list, at the cross (or continuing the chain, as a part letter)", "k", "" },
        { "Parts", "Select the next / previous part in the component list (the canvas keeps the keys)", ": / /", "" },
        { "Switches", "Operate a switch or push button (its Key property, A–Z)", "Shift+its Key", "Simulation running" },
        // K1: keyboard cursor (revised: independent of the mouse)
        { "Moving", "Move the keyboard cursor (yellow cross) a big step (64 px)", "Arrows", "Nothing selected" },
        { "Moving", "Move the keyboard cursor one grid step (8 px)", "Shift+Arrows", "Nothing selected" },
        { "Moving", "Place the keyboard cursor", "Click on empty canvas", "" },
        { "View", "Pan mode: the arrows (Shift: small steps) pan the canvas, until another key", "h", "" },
        { "View", "Zoom in", "Cmd+= or Cmd++", "" },
        { "View", "Zoom out", "Cmd+-", "" },
        { "View", "Actual size (100 %)", "Cmd+0", "" },
        // K2: moving the selection
        { "Moving", "Move the selection a big step (64 px), wires follow", "Arrows", "Selection" },
        { "Moving", "Move the selection one grid step (8 px), wires follow", "Shift+Arrows", "Selection" },
        // K3: pins
        { "Pins & wiring", "Next pin clockwise (the pin to wire to)", ">", "One part selected" },
        { "Pins & wiring", "Previous pin (the pin to wire to)", "<", "One part selected" },
        { "Pins & wiring", "Next pin, as the pin to wire from", "Cmd+>", "One part selected" },
        { "Pins & wiring", "Previous pin, as the pin to wire from", "Cmd+<", "One part selected" },
        // K8: wiring
        { "Pins & wiring", "Wire pinFrom to pinTo (a dot is inserted on a pin that already has a wire)", "w", "" },
        // K4: selection
        { "Selection", "Select the next part (reading order); the nearest to the cursor when none is", "Tab", "" },
        { "Selection", "Select the previous part", "Shift+Tab", "" },
        { "Selection", "Unselect all", "Esc", "Selection" },
        { "Selection", "Unselect the part; the cursor goes a big step beyond its selected pin, "
                       "in the pin's direction (a part without pins: right of its centre)", "Enter", "One part selected" },
        { "Selection", "As Enter, but beyond the most opposite pin of the part", "Shift+Enter", "One part selected" },
        // K9: network mode
        { "Wires", "Network mode: walk the wires, corners and dots of the selected pin", "=", "One part selected, its pin wired" },
        { "Wires", "Network mode from the dot, corner or wire nearest to the cross", "=", "Nothing selected" },
        { "Wires", "Next / previous wire, corner or dot (nearest along the wires first)", "Tab / Shift+Tab", "Network mode" },
        { "Wires", "Move the selected dot or corner 64 / 8 px (the wire stays horizontal / vertical)", "Arrows / Shift+Arrows", "Network mode" },
        { "Wires", "Delete the selected wire", "Backspace", "Network mode" },
        { "Wires", "Back to parts mode, the part selected (any other key too, then it acts)", "= or Esc", "Network mode" },
        // K9b: drawing a wire from a wire
        { "Wires", "Draw a new wire from the wire under the cross (a dot is added there)", "+", "Nothing selected" },
        { "Wires", "Move the end of the wire being drawn (it follows horizontally / vertically)", "Arrows / Shift+Arrows", "Drawing a wire" },
        { "Wires", "Fix a corner at the cross", "=", "Drawing a wire" },
        { "Wires", "Remove the last fixed corner", "Backspace", "Drawing a wire" },
        { "Wires", "Connect to the pin or wire at the cross (8 px)", "Enter", "Drawing a wire" },
        { "Wires", "Cancel the wire (and its dot)", "Esc", "Drawing a wire" },
        // K6: properties
        { "Edit", "Open the selected part's properties dialog (closing it returns to the canvas)", "Space", "One part selected" },
        // K7: typing a value
        { "Edit", "Type the selected part's value: digits, - . , and p n u m k M G (capitals too; "
                  "4k7 = 4.7 k; no multiplier keeps the part's one). Enter sets it (one undo step), "
                  "Esc or any other action reverts", "0-9  -  .  ,", "One part selected" },
    };
    return list;
}
