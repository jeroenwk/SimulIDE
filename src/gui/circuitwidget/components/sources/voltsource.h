/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef VOLTSOURCE_H
#define VOLTSOURCE_H

#include "varsource.h"

class LibraryItem;
class IoPin;

class VoltSource : public VarSource
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( VoltSource )
        VoltSource( QString type, QString id );

        virtual double nominalVolt() override { return qMax( qAbs( m_maxValue ), qAbs( m_minValue ) ); }
        ~VoltSource();

        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        virtual void updateStep() override;

    private:
        IoPin* m_outPin;
};

#endif
