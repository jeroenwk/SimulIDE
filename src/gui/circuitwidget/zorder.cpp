/***************************************************************************
 *   Stacking order of the graphical shapes (specs/gfx-arrange.md)         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QSet>
#include <QDebug>

#include "zorder.h"
#include "shape.h"
#include "circuit.h"

static bool isShape( QGraphicsItem* item, Shape** out )
{
    if( item->type() != QGraphicsItem::UserType+1 || item->parentItem() ) return false;
    Component* comp = qgraphicsitem_cast<Component*>( item );
    Shape* s = comp ? dynamic_cast<Shape*>( comp ) : nullptr;
    if( out ) *out = s;
    return s;
}

QList<Shape*> ZOrder::selectedShapes()
{
    QList<Shape*> list;
    Circuit* circ = Circuit::self();
    if( !circ ) return list;
    Shape* s;
    for( QGraphicsItem* item : circ->selectedItems() ) if( isShape( item, &s ) ) list.append( s );
    return list;
}

QList<Shape*> ZOrder::stack()
{
    QList<Shape*> list;
    Circuit* circ = Circuit::self();
    if( !circ ) return list;
    Shape* s;                                            // Stacking order: Z, then insertion
    for( QGraphicsItem* item : circ->items( Qt::AscendingOrder ) ) if( isShape( item, &s ) ) list.append( s );
    return list;
}

QString ZOrder::opName( op_t op )
{
    switch( op ){
        case toFront:  return QObject::tr("Bring to Front");
        case forward:  return QObject::tr("Bring Forward");
        case backward: return QObject::tr("Send Backward");
        case toBack:   return QObject::tr("Send to Back");
    }
    return "";
}

QString ZOrder::opKeys( op_t op )
{
    switch( op ){
        case toFront:  return "Ctrl+Shift+F";
        case forward:  return "Ctrl+Alt+Shift+F";
        case backward: return "Ctrl+Alt+Shift+B";
        case toBack:   return "Ctrl+Shift+B";
    }
    return "";
}

// One step: past the nearest shape that overlaps it on that side (a step past a shape it
// does not touch changes nothing visible), else past the next one
static int stepTarget( const QList<Shape*>& order, const QSet<Shape*>& sel, int i, int dir )
{
    QRectF r = order[i]->sceneBoundingRect();
    int first = -1;
    for( int j=i+dir; j>=0 && j<order.size(); j+=dir )
    {
        if( sel.contains( order[j] ) ) continue;
        if( first < 0 ) first = j;
        if( order[j]->sceneBoundingRect().intersects( r ) ) return j;
    }
    return first;
}

QString ZOrder::apply( op_t op, QList<Shape*> shapes )
{
    if( shapes.isEmpty() ) return QObject::tr("Arrange: no shape selected (Rectangle, Ellipse, Line, Image)");
    QList<Shape*> order = stack();
    QSet<Shape*> sel;
    for( Shape* s : shapes ) sel.insert( s );
    QList<Shape*> newOrder;

    if( op == toFront || op == toBack )
    {
        QList<Shape*> mine, others;
        for( Shape* s : order ) ( sel.contains( s ) ? mine : others ).append( s );
        newOrder = ( op == toFront ) ? others+mine : mine+others;
    }
    else{
        newOrder = order;
        int dir = ( op == forward ) ? 1 : -1;
        int n = newOrder.size();
        for( int k=0; k<n; ++k )                         // The one nearest the end it goes to first
        {
            int i = ( dir > 0 ) ? n-1-k : k;
            Shape* s = newOrder[i];
            if( !sel.contains( s ) ) continue;
            int j = stepTarget( newOrder, sel, i, dir );
            if( j < 0 ) continue;
            newOrder.removeAt( i );
            newOrder.insert( j, s );                     // Removing i shifted j by one: lands past it
        }
    }
    if( newOrder == order )
        return ( op == toFront || op == forward ) ? QObject::tr("Arrange: already in front")
                                                  : QObject::tr("Arrange: already at the back");

    // The Z values in use are the slots, made strictly increasing (ties pushed down), so the
    // shapes keep their place relative to the parts (Z 0) and wires (100)
    QList<double> zSlots;
    for( Shape* s : order ) zSlots.append( s->zValue() );
    std::sort( zSlots.begin(), zSlots.end() );
    for( int k=zSlots.size()-2; k>=0; --k ) if( zSlots[k] >= zSlots[k+1] ) zSlots[k] = zSlots[k+1]-1;

    Circuit* circ = Circuit::self();
    circ->beginCircuitBatch();
    for( int k=0; k<newOrder.size(); ++k )
    {
        Shape* s = newOrder[k];
        if( s->zValue() == zSlots[k] ) continue;
        circ->addCompChange( s->getUid(), "Z_Value", s->getPropStr("Z_Value") );
        s->setZVal( zSlots[k] );
    }
    circ->saveChanges();
    qDebug() << "[arrange]" << opName( op ) << shapes.size() << "shape(s)";
    return "";
}
