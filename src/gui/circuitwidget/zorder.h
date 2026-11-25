/***************************************************************************
 *   Stacking order of the graphical shapes (specs/gfx-arrange.md)         *
 *                                                                         *
 *   Bring to front / forward, send backward / to back for the selected    *
 *   shapes (Rectangle, Ellipse, Line, Image): their Z Value is set, one    *
 *   undo step. The order is among the shapes only; the Z values they      *
 *   already use are kept as the slots, so shapes behind the parts stay    *
 *   behind them.                                                          *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef ZORDER_H
#define ZORDER_H

#include <QList>
#include <QString>

class Shape;
class Component;

class ZOrder
{
    public:
        enum op_t { toFront=0, forward, backward, toBack };

        // The selected shapes (top level)
        static QList<Shape*> selectedShapes();

        // Applies op to `shapes`; returns a line for Messages when nothing changed
        // (already in front / at the back, no shape), else ""
        static QString apply( op_t op, QList<Shape*> shapes );

        static QString opName( op_t op );
        static QString opKeys( op_t op );        // In KeyText::native's notation ("Ctrl+Shift+F")

        // All top-level shapes, back to front (Z value, then the scene's own order)
        static QList<Shape*> stack();
};

#endif
