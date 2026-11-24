/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "ellipse.h"

#define tr(str) simulideTr("Ellipse",str)

Component* Ellipse::construct( QString type, QString id )
{ return new Ellipse( type, id ); }

LibraryItem* Ellipse::libraryItem()
{
    return new LibraryItem(
        tr("Ellipse"),
        "Graphical",
        "ellipse.png",
        "Ellipse",
        Ellipse::construct);
}

Ellipse::Ellipse( QString type, QString id )
       : Shape( type, id )
{
}
Ellipse::~Ellipse(){}

QPainterPath Ellipse::shape() const
{
    QPainterPath path;
    path.addEllipse( Shape::boundingRect() );
    return path;
}

void Ellipse::paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
    Component::paint( p, option, widget );

    // The 1 px line is the border when Border is 0; under a border its dots
    // would show in the border's gaps
    p->setPen( m_border > 0 ? QPen( Qt::NoPen ) : BorderStyle::pen( m_borderColor, 0, m_borderStyle ) );
    p->setBrush( m_color );

    double opacity = p->opacity();
    p->setOpacity( opacity*m_opac );
    p->drawEllipse( m_area );
    p->setOpacity( opacity );

    if( m_border > 0 )
    {
        p->setPen( BorderStyle::pen( m_borderColor, m_border, m_borderStyle ) );
        p->setBrush( Qt::transparent );
        p->drawEllipse( m_area );
    }
    Component::paintSelected( p );
}
