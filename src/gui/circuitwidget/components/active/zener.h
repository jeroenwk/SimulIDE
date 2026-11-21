/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef ZENER_H
#define ZENER_H

#include "diode.h"

class LibraryItem;

class Zener : public Diode
{
    public:
        Zener( QString type, QString id );
        ~Zener();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Zener )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();
};

#endif
