/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef VOLTREG_H
#define VOLTREG_H

#include <QtGlobal>
#include "component.h"
#include "e-resistor.h"

class LibraryItem;

class VoltReg : public Component, public eResistor
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( VoltReg )
        VoltReg( QString type, QString id );
        ~VoltReg();
        
        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        virtual void stamp() override;
        virtual void updateStep() override;
        virtual void voltChanged() override;
        virtual void initialize() override;
        virtual void stepSettled() override;

        double outVolt() { return m_vRef; }
        void setOutVolt( double v ) { m_vRef = v; m_changed = true; }

        // Minimum input-output difference (V); below it the output follows the input
        double dropout() { return m_drop; }
        void setDropout( double v ) { m_drop = qMax( 0.0, v ); m_changed = true; }

        // Current out of the ref (ADJ) pin, from the input (A; LM317: 50 uA)
        double iAdj() { return m_iAdj; }
        void setIAdj( double i ) { m_iAdj = i; m_changed = true; }

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget );

    protected:
        double m_vRef;               // < 0: a negative regulator (79xx, LM337), the same law mirrored
        double m_drop;
        double m_iAdj;
        double m_lastCurrent;

        bool m_connected;
        int  m_region;               // Part state: -1 none, 0 off, 1 dropout, 2 regulating
};

#endif
