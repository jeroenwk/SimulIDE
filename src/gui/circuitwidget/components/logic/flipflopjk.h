/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef FLIPFLOPJK_H
#define FLIPFLOPJK_H

#include "flipflopbase.h"

class LibraryItem;

class FlipFlopJK : public FlipFlopBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( FlipFlopJK )
        FlipFlopJK( QString type, QString id );
        ~FlipFlopJK();

        static Component* construct( QString type, QString id );
        static LibraryItem *libraryItem();

    protected:
        void calcOutput();
};

#endif
