/***************************************************************************
 *   Copyright (C) 2017 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef AMPERIMETER_H
#define AMPERIMETER_H

#include "meter.h"

class LibraryItem;

class Amperimeter : public Meter
{
    public:
        Amperimeter( QString type, QString id );
        ~Amperimeter();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Amperimeter )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void updateStep() override;

    protected:
        virtual double instant() override;
};

#endif
