/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "resistor.h"
#include "itemlibrary.h"
#include "pin.h"

#include "doubleprop.h"

#define tr(str) simulideTr("Resistor",str)

Component* Resistor::construct( QString type, QString id )
{ return new Resistor( type, id ); }

LibraryItem* Resistor::libraryItem()
{
    return new LibraryItem(
        tr("Resistor"),
        "Resistors",
        "resistor.png",
        "Resistor",
        Resistor::construct);
}

Resistor::Resistor( QString type, QString id )
        : Comp2Pin( type, id )
        , eResistor( id )
        , m_cp( id+"-Cp" )
{
    m_cp.setPins( [this](){ return std::make_pair( (ePin*)m_pin[0], (ePin*)m_pin[1] ); } );
    m_cp.setMj( 0 );

    m_ePin[0] = m_pin[0];
    m_ePin[1] = m_pin[1];
    setThermalNoise( true );   // Transient noise: a physical resistor

    m_pin[0]->setLength( 5 );
    m_pin[1]->setLength( 5 );

    addPropGroup( { tr("Main"), {
        new DoubProp<Resistor>("Resistance", tr("Resistance"), "Ω"
                              , this, &Resistor::getRes, &Resistor::setResSafe ),
        new DoubProp<Resistor>("Cp", tr("Parallel Capacitance (0 = none)"), "pF"
                              , this, &Resistor::cp, &Resistor::setCp )
    }, 0 } );

    setShowProp("Resistance");
    setPropStr("Resistance", "100");
}
Resistor::~Resistor(){}

void Resistor::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    p->drawRect( m_area );

    Component::paintSelected( p );
}

void Resistor::simState( StateIO& s )
{
    eResistor::simState( s );
}
