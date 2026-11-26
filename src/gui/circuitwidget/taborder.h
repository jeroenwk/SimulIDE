/***************************************************************************
 *   Tab order of the parts (specs/parts-pane.md §12)                      *
 ***( see copyright.txt file at root folder )*******************************/

// The canvas Tab walk: parts with a tab index (Component::tabIndex, saved in the .sim1) in
// that order, then the others in reading order (rows top to bottom, left to right). Locked
// and hidden parts are not in it. Every change writes the indices 1..n of the whole walk,
// one undo step; reset() clears them (reading order again).

#ifndef TABORDER_H
#define TABORDER_H

#include <QList>
#include <QString>

class Component;

class TabOrder
{
    public:
 static QList<Component*> walk();

        // "Tab order" submenu: as selected, to front, to back, reset. Acts on the selection
        // when it holds `on` (or on is null), else on that part alone
 static void addMenu( class QMenu* menu, Component* on );

        // Each returns the line written to Messages ("" when nothing changed)
 static QString asSelected( QList<Component*> inSelectionOrder );   // One block where the first of them was
 static QString toFront( QList<Component*> parts );
 static QString toBack( QList<Component*> parts );
 static QString moveTo( QList<Component*> parts, int place );       // 1-based: they start there, the others shift
 static QString reset();
 static bool    custom();                                           // Any part has an index

    private:
 static QString apply( QList<Component*> order, QString what );
 static QList<Component*> inWalk( QList<Component*> parts, QList<Component*> walk, QStringList* left );
};

#endif
