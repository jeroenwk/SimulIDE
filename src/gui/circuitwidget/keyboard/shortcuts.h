/***************************************************************************
 *   Shortcut registry (specs/keyboard-input.md §3)                         *
 *                                                                         *
 *   One table of every keyboard shortcut of the circuit editor. The       *
 *   Settings > Shortcuts list (K10) and the CLI (kbd shortcuts) read it.  *
 *   Add a row here whenever a key gets a meaning.                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SHORTCUTS_H
#define SHORTCUTS_H

#include <QString>
#include <QList>

struct Shortcut
{
    QString category;   // File, Edit, View, Selection, Moving, ...
    QString action;     // What it does
    QString keys;       // As typed on macOS: "Cmd+Shift+Z", "Shift+Arrows"
    QString context;    // When it applies; empty: always on the canvas
};

class Shortcuts
{
    public:
 static const QList<Shortcut>& all();
};

#endif
