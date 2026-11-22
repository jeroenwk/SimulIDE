/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef VOLTIMETER_H
#define VOLTIMETER_H

#include "meter.h"

class LibraryItem;

class Voltimeter : public Meter
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Voltimeter )
        Voltimeter( QString type, QString id );
        ~Voltimeter();

        static Component* construct( QString type, QString id );
        static LibraryItem *libraryItem();

        virtual void updateStep() override;

    protected:
        virtual double instant() override;
};

#endif
