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
        { "Parts", "Add the part bound to a letter at the keyboard cursor (a part selected: a big step right of it), selected", "its letter", "Set in Manage Components" },
        { "Parts", "Add the part bound to a letter, at the mouse", "Alt+its letter", "Set in Manage Components" },
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
        // K4: selection
        { "Selection", "Select the next part (reading order); the nearest to the cursor when none is", "Tab", "" },
        { "Selection", "Select the previous part", "Shift+Tab", "" },
        { "Selection", "Unselect all", "Esc", "Selection" },
        { "Selection", "Unselect the part, put the cursor a big step right of it", "Enter", "One part selected" },
    };
    return list;
}
