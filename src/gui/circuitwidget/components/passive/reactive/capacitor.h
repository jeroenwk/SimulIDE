/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CAPACITOR_H
#define CAPACITOR_H

#include "capacitorbase.h"

class LibraryItem;

class Capacitor : public CapacitorBase
{
    public:
        Capacitor( QString type, QString id );
        ~Capacitor();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Capacitor )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;
};

#endif
