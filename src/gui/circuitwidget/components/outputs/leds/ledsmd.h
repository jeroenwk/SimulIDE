/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef LEDSMD_H
#define LEDSMD_H

#include "ledbase.h"

class LedSmd : public LedBase
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( LedSmd )
        LedSmd( QString type, QString id, QRectF area, ePin* pin0=NULL, ePin* pin1=NULL );
        ~LedSmd();

    protected:
        void drawBackground( QPainter* p );
        void drawForeground( QPainter* p );
};

#endif
