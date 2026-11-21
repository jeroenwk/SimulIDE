/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef FLIPFLOPT_H
#define FLIPFLOPT_H

#include "flipflopbase.h"

class LibraryItem;

class FlipFlopT : public FlipFlopBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( FlipFlopT )
        FlipFlopT( QString type, QString id );
        ~FlipFlopT();

        static Component* construct( QString type, QString id );
        static LibraryItem *libraryItem();

    protected:
        void calcOutput();
};

#endif
