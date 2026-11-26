/***************************************************************************
 *   Shortcut registry (specs/keyboard-input.md §3)                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QKeySequence>
#include <QRegularExpression>
#include <QCoreApplication>

#include "shortcuts.h"
#include "componentselector.h"

const QList<Shortcut>& Shortcuts::all()
{
    static const QList<Shortcut> list = {
        // Existing shortcuts (Circuit::keyPressEvent); the file keys also off the canvas
        // (CircuitWidget::fileKey), except in the code editor
        { "File", "New circuit",            "Cmd+N",       "Not in the code editor, which uses the same keys for its file" },
        { "File", "Open circuit",           "Cmd+O",       "Not in the code editor, which uses the same keys for its file" },
        { "File", "Save circuit",           "Cmd+S",       "Not in the code editor, which uses the same keys for its file" },
        { "File", "Save circuit as",        "Cmd+Shift+S", "Not in the code editor, which uses the same keys for its file" },
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
        { "Edit", "Lock the selected parts: clicks, rubber bands, Tab, Space and Cmd+A pass them by; not moved, rotated or deleted", "Cmd+Shift+K", "Selection" },
        { "Edit", "Unlock the selected parts (select them in the Parts pane)", "Opt+Cmd+Shift+K", "Locked parts selected" },
        { "Selection", "Tab order: the selected parts follow each other in the order they were selected, where the first of them was", "Cmd+Shift+T", "Two or more parts selected" },
        { "Selection", "Tab order: the selected parts to the front of the walk", "Opt+Cmd+T", "Selection" },
        { "Selection", "Tab order: the selected parts to the back of the walk", "Opt+Cmd+Shift+T", "Selection" },
        { "Selection", "Select all",        "Cmd+A",       "" },
        { "Selection", "Cancel the wire being drawn, stop linking", "Esc", "" },
        { "Parts", "Add the part bound to a letter at the keyboard cursor. A part selected (or the cross still where "
                   "Enter left it, beyond a pin): the new part's first pin faces that pin, a big step beyond it, in "
                   "a straight line; the part is rotated to match", "its letter", "Set in the part's properties (Keyboard tab)" },
        { "Parts", "Add the part bound to a letter, at the mouse", "Alt+its letter", "Set in the part's properties (Keyboard tab)" },
        { "Parts", "Add the part selected in the component list, at the cross (or continuing the chain, as a part letter); "
                   "on a group: expand / collapse it", "k", "" },
        { "Parts", "Select the next / previous row of the component list, groups included (the canvas keeps the keys)", ": / /", "" },
        { "Switches", "Operate a switch or push button (its Key property, A–Z)", "Shift+its Key", "Simulation running" },
        // K1: keyboard cursor (revised: independent of the mouse)
        { "Moving", "Move the keyboard cursor (yellow cross) one grid step (8 px)", "Arrows", "Nothing selected" },
        { "Moving", "Move the keyboard cursor a big step (64 px)", "Cmd+Arrows", "Nothing selected" },
        { "Selection", "Selection rectangle from the cross, 8 / 64 px steps: the parts it touches are selected; "
                       "releasing Shift keeps them, Esc cancels", "Shift+Arrows / Cmd+Shift+Arrows", "Nothing selected" },
        { "Moving", "Place the keyboard cursor", "Click on empty canvas", "" },
        { "View", "Pan mode: the arrows (Shift: small steps) pan the canvas, until another key", "h", "" },
        { "View", "Zoom in", "Cmd+= or Cmd++", "" },
        { "View", "Zoom out", "Cmd+-", "" },
        { "View", "Actual size (100 %)", "Cmd+0", "" },
        // Toolbar keys (tooltips show them in the platform's notation)
        { "View", "Zoom to fit the whole circuit", "Opt+Cmd+0", "" },
        { "View", "Zoom to fit the selection", "Ctrl+Cmd+0", "Selection" },
        { "View", "Show current flow (toggle)", "F7", "" },
        { "View", "Show capacitor charge and inductor current (toggle)", "F8", "" },
        { "View", "Show part states (toggle)", "F9", "" },
        { "View", "Show instruments (toggle)", "F10", "" },
        { "Simulation", "Start / stop the simulation", "F5", "" },
        { "Simulation", "Pause / resume the simulation", "F6", "Simulation running" },
        // Simulation keys (specs/sim-control.md §3.2): only while the canvas has no keyboard focus.
        // A start takes it from the canvas; a click gives it back; the skip box is framed blue then
        { "Simulation", "Start / pause / resume the simulation", "Space", "Canvas without focus" },
        { "Simulation", "Stop the simulation; during a skip: cancel the skip (stays paused)", "Esc", "Canvas without focus" },
        { "Simulation", "Skip interval: next / previous value (1 step, 1 ps … 1 min, 1–2–5)", "Up / Down", "Canvas without focus" },
        { "Simulation", "Skip interval ×5 / ×10 / ×100 (Down: ÷)", "Shift+Up / Opt+Up / Shift+Opt+Up", "Canvas without focus" },
        { "Simulation", "Pause and skip forward by the skip interval; stopped: start, then skip", "Right", "Canvas without focus" },
        { "Simulation", "Skip forward 5× / 10× / 100× the skip interval", "Shift+Right / Opt+Right / Shift+Opt+Right", "Canvas without focus" },
        { "Simulation", "Pause and go back by the skip interval (Shift/Opt: 5×/10×/100×), with the keyframes of the run", "Left", "Canvas without focus" },
        { "Simulation", "Give the keys back to the simulation", "Esc", "Simulation on, canvas focused, nothing selected or in progress" },
        // Saved key frames (specs/sim-control.md §5.1)
        { "Simulation", "Save a key frame of the paused state (Key frames tab), then type its description", "Cmd+K", "Simulation paused, circuit saved" },
        { "Simulation", "Key frames list: go to the key frame (Up / Down go along), Shift / Cmd select several", "Click", "Key frames tab" },
        { "Simulation", "Key frames list: play from the earliest selected key frame to the latest", "Space / F5 / F6", "Two or more key frames selected" },
        { "Simulation", "Key frames list: edit the description", "Enter / double-click", "Key frames tab" },
        { "Simulation", "Key frames list: delete the selected key frames (asks first)", "Backspace", "Key frames tab" },
        { "Simulation", "Transient noise (toggle)", "F12", "" },
        { "File", "Export as image (PNG, SVG)", "Cmd+P", "" },
        { "File", "Settings", "Cmd+,", "" },
        // K2: moving the selection
        { "Moving", "Move the selection one grid step (8 px), wires follow", "Arrows", "Selection" },
        { "Moving", "Move the selection a big step (64 px), wires follow", "Cmd+Arrows", "Selection" },
        // K3: pins
        { "Pins & wiring", "Next pin clockwise (the pin to wire to)", ">", "One part selected" },
        { "Pins & wiring", "Previous pin (the pin to wire to)", "<", "One part selected" },
        { "Pins & wiring", "Next pin, as the pin to wire from", "Cmd+>", "One part selected" },
        { "Pins & wiring", "Previous pin, as the pin to wire from", "Cmd+<", "One part selected" },
        // K8: wiring
        { "Pins & wiring", "Wire pinFrom to pinTo (a dot is inserted on a pin that already has a wire)", "w", "" },
        // K4: selection
        { "Selection", "Select the next part (reading order); the nearest to the cursor when none is", "Tab", "" },
        { "Selection", "Select the part or wire nearest to the cross (a wire: network mode on it)", "Space", "Nothing selected" },
        { "Selection", "Select the previous part", "Shift+Tab", "" },
        { "Selection", "Unselect all", "Esc", "Selection" },
        { "Selection", "Unselect the part; the cursor goes a big step beyond its selected pin, "
                       "in the pin's direction (a part without pins: right of its centre)", "Enter", "One part selected" },
        { "Selection", "As Enter, but beyond the most opposite pin of the part", "Shift+Enter", "One part selected" },
        // K9: network mode
        { "Wires", "Network mode: walk the wires, corners and dots of the selected pin", "=", "One part selected, its pin wired" },
        { "Wires", "Network mode from the dot, corner or wire nearest to the cross", "=", "Nothing selected" },
        { "Wires", "Next / previous wire, corner or dot (nearest along the wires first)", "Tab / Shift+Tab", "Network mode" },
        { "Wires", "Move the selected dot or corner 8 / 64 px (the wire stays horizontal / vertical)", "Arrows / Cmd+Arrows", "Network mode" },
        { "Wires", "Delete the marked wires, or the selected wire", "Backspace", "Network mode" },
        { "Wires", "Mark / unmark the selected wire (several wires)", "Enter", "Network mode" },
        { "Wires", "Mark every wire of the net (not instrument leads); again: unmark them", "*", "Network mode" },
        { "Wires", "Wire colours: line, band, opacity, width (marked wires, or the selected one)", "Space", "Network mode" },
        { "Wires", "Wire colours of the wire segments selected by a rubber band", "Space", "Only wires selected" },
        { "Wires", "Back to parts mode, the part selected (any other key too, then it acts)", "= or Esc", "Network mode" },
        // K9b: drawing a wire from a wire
        { "Wires", "Draw a new wire from the wire under the cross (a dot is added there)", "+", "Nothing selected" },
        { "Wires", "Move the end of the wire being drawn 8 / 64 px (it follows horizontally / vertically)", "Arrows / Cmd+Arrows", "Drawing a wire" },
        { "Wires", "Fix a corner at the cross", "=", "Drawing a wire" },
        { "Wires", "Remove the last fixed corner", "Backspace", "Drawing a wire" },
        { "Wires", "Connect to the pin or wire at the cross (8 px)", "Enter", "Drawing a wire" },
        { "Wires", "Cancel the wire (and its dot)", "Esc", "Drawing a wire" },
        // K6: properties
        { "Edit", "Open the selected part's properties dialog (closing it returns to the canvas)", "Space", "One part selected" },
        { "Shapes", "Bring to front (Rectangle, Ellipse, Line, Image)", "Cmd+Shift+F", "Shapes selected" },
        { "Shapes", "Bring forward, past the next shape it overlaps", "Opt+Cmd+Shift+F", "Shapes selected" },
        { "Shapes", "Send backward, past the next shape it overlaps", "Opt+Cmd+Shift+B", "Shapes selected" },
        { "Shapes", "Send to back", "Cmd+Shift+B", "Shapes selected" },
        { "Shapes", "Highlight the corner nearest to the cross (a Line: its ends); a click on a corner handle does too", "=", "One shape selected" },
        { "Shapes", "Next / previous corner, clockwise", "Tab / Shift+Tab", "A shape's corner highlighted" },
        { "Shapes", "Move the corner 8 / 64 px: the shape is resized, the opposite corner stays (a drag of the handle too)", "Arrows / Cmd+Arrows", "A shape's corner highlighted" },
        { "Shapes", "Leave the corner, the shape stays selected (any other key too, then it acts)", "= or Esc", "A shape's corner highlighted" },
        // K7: typing a value
        { "Edit", "Type the selected part's value: digits, - . , and p n u m k M G (capitals too; "
                  "4k7 = 4.7 k; no multiplier keeps the part's one). Enter sets it (one undo step), "
                  "Esc or any other action reverts", "0-9  -  .  ,", "One part selected" },
    };
    return list;
}

// Each run of modifiers ("Cmd+Shift+") is written as Qt writes it on this platform
// (a key sequence's native text, without its key). Cmd is Qt's Ctrl, Ctrl is Qt's
// Meta (the Control key on macOS, the Windows key elsewhere). On macOS a word key
// gets a space: "⇧ Tab", "⌥ its letter"; a character does not: "⌘Z"
QString Shortcuts::shown( const QString& keys )
{
    static const QRegularExpression mods("((?:Cmd|Ctrl|Shift|Opt|Alt)\\+)+(?=\\S)");
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = mods.globalMatch( keys );
    while( it.hasNext() )
    {
        QRegularExpressionMatch m = it.next();
        out += keys.mid( last, m.capturedStart()-last );
        int k = 0;
        for( QString mod : m.captured( 0 ).split('+', QString::SkipEmptyParts ) )
        {
            if     ( mod == "Cmd" )   k |= Qt::CTRL;
            else if( mod == "Ctrl" )  k |= Qt::META;
            else if( mod == "Shift" ) k |= Qt::SHIFT;
            else                      k |= Qt::ALT;   // Opt, Alt
        }
        QString prefix = QKeySequence( k | Qt::Key_A ).toString( QKeySequence::NativeText );
        prefix.chop( 1 );                                // The A
        last = m.capturedEnd();
        int end = last;                                  // The key after it
        while( end < keys.size() && !keys.at( end ).isSpace() ) end++;
        bool word = end-last > 1 && keys.at( last ).isLetter();
        if( word && !prefix.endsWith('+') ) prefix += ' ';
        out += prefix;
    }
    out += keys.mid( last );
    return out;
}

QStringList Shortcuts::categories()
{
    return { "File", "Edit", "View", "Simulation", "Selection", "Moving", "Pins & wiring",
             "Wires", "Shapes", "Parts", "Part letters", "Switches" };
}

QList<Shortcut> Shortcuts::table()
{
    QList<Shortcut> rows;
    for( const Shortcut& s : all() ) rows.append({ s.category, s.action, shown( s.keys ), s.context });

    if( ComponentSelector* sel = ComponentSelector::self() )   // The part letters in effect
    {
        QHash<QString, QString> keys = sel->getShortCuts();
        QStringList names = keys.keys();
        std::sort( names.begin(), names.end(), [&keys]( const QString& a, const QString& b ){
            return keys.value( a ) < keys.value( b ); } );
        for( const QString& n : names )
            if( !keys.value( n ).isEmpty() )
                rows.append({ "Part letters", QCoreApplication::translate("Shortcuts", "Add %1").arg( sel->caption( n ) ),
                              keys.value( n ), "" });
    }
    QStringList order = categories();
    std::stable_sort( rows.begin(), rows.end(), [&order]( const Shortcut& a, const Shortcut& b ){
        int ia = order.indexOf( a.category ), ib = order.indexOf( b.category );
        if( ia < 0 ) ia = order.size();                  // Unknown: last, as found
        if( ib < 0 ) ib = order.size();
        return ia < ib; } );
    return rows;
}
