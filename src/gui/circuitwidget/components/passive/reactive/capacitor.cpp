/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "capacitor.h"
#include "itemlibrary.h"

#define tr(str) simulideTr("Capacitor",str)

Component* Capacitor::construct( QString type, QString id )
{ return new Capacitor( type, id ); }

LibraryItem* Capacitor::libraryItem()
{
    return new LibraryItem(
        tr("Capacitor"),
        "Reactive",
        "capacitor.png",
        "Capacitor",
        Capacitor::construct);
}

Capacitor::Capacitor( QString type, QString id )
         : CapacitorBase( type, id )
{}
Capacitor::~Capacitor(){}

void Capacitor::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    QPen pen = p->pen();
    pen.setWidth(3);
    p->setPen(pen);

    QPen base = pen;
    setPlatePen( p, 0 );                // Coloured by node potential while the charge display is on
    p->drawLine(-3,-6,-3, 6 );
    p->setPen( base );
    setPlatePen( p, 1 );
    p->drawLine( 3,-6, 3, 6 );

    Component::paintSelected( p );
}
