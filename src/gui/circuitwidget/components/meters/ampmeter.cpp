/***************************************************************************
 *   Copyright (C) 2017 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "ampmeter.h"
#include "e-node.h"
#include "e-pin.h"
#include "itemlibrary.h"

#define tr(str) simulideTr("Amperimeter",str)

Component* Amperimeter::construct( QString type, QString id )
{ return new Amperimeter( type, id ); }

LibraryItem* Amperimeter::libraryItem()
{
    return new LibraryItem(
        tr("Ampmeter"),
        "Meters",
        "amperimeter.png",
        "Amperimeter",
        Amperimeter::construct);
}

Amperimeter::Amperimeter( QString type, QString id )
           : Meter( type, id )
{
    m_unit = "A";
    m_dispValue = 0;
    setRes( 1e-6 );
    m_display.setText(" 0.000\n A");
}
Amperimeter::~Amperimeter(){}

void Amperimeter::updateStep()
{
    double curr = measured();
    
    if( curr != m_dispValue )
    {
        m_dispValue = curr;
        Meter::updateStep();
}   }

// The meter is 1 uOhm: dV*1e6 loses the current in the rounding of the node voltages (steps of
// 0.2 nA per volt, create-circuit #687). The current that leaves the meter is also what the rest
// of the node takes (KCL): read it on the side whose other stamps are the softest.
double Amperimeter::instant()
{
    eNode* nl = m_ePin[0]->getEnode();
    eNode* nr = m_ePin[1]->getEnode();
    double gl = 1e300, gr = 1e300, il = 0, ir = 0;
    if( nl ) il =  nl->othersCurrent( m_ePin[0], &gl );   // into the meter at lPin
    if( nr ) ir = -nr->othersCurrent( m_ePin[1], &gr );   // out of the meter at rPin
    double g = qMin( gl, gr );
    if( g > 1e3 ) return current();                       // both sides stiff: dV/R is as good
    return gl <= gr ? il : ir;
}

void Amperimeter::simState( StateIO& s )
{
    Meter::simState( s );
}
