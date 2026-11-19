/***************************************************************************
 *   Shortcut texts in the platform's notation                              *
 *                                                                         *
 *   Keys are written in Qt's portable form ("Ctrl+Shift+S", "F5"): Ctrl   *
 *   is the Command key on macOS. Shown, they read as the platform writes  *
 *   them: ⇧⌘S on macOS, Ctrl+Shift+S on Windows and Linux.                *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef KEYTEXT_H
#define KEYTEXT_H

#include <QString>
#include <QKeySequence>

class QAction;

namespace KeyText
{
    // "Ctrl+N" -> "⌘N" (macOS), "Ctrl+N" (others)
    QString native( const QString& portable );
    QString native( const QKeySequence& keys );

    // The action's tooltip: its text and key, "Stop Simulation (F5)". Kept as the text
    // changes (a mnemonic &, a "\t..." suffix and a trailing "..." are left out)
    void tipWithKey( QAction* action, const QString& portable );
}

#endif
