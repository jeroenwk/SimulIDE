/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef RESISTOR_H
#define RESISTOR_H

#include "e-resistor.h"
#include "comp2pin.h"
#include "e-junctioncap.h"

class LibraryItem;

class Resistor : public Comp2Pin, public eResistor
{
    public:
        Resistor( QString type, QString id );
        ~Resistor();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Resistor )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        // Parallel (parasitic) capacitance across the two pins, 0 = none (costs nothing then).
        // Its current flows through its own pins: the resistor's current (V/R) does not include it
        double cp() { return m_cp.cj0(); }
        void setCp( double c ) { m_cp.setCj0( c ); }

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    protected:
        eJunctionCap m_cp;   // Constant (M = 0), never a noise default
};

#endif

