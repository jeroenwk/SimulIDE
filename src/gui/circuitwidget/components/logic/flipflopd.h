/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef FLIPFLOPD_H
#define FLIPFLOPD_H

#include "flipflopbase.h"

class LibraryItem;

class FlipFlopD : public FlipFlopBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( FlipFlopD )
        FlipFlopD( QString type, QString id );
        ~FlipFlopD();

        static Component* construct( QString type, QString id );
        static LibraryItem *libraryItem();

    protected:
        void calcOutput();
};

#endif
