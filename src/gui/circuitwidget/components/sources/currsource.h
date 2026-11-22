/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CURRSOURCE_H
#define CURRSOURCE_H

#include "varsource.h"

class LibraryItem;
class Pin;

class CurrSource : public VarSource
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( CurrSource )
        CurrSource( QString type, QString id );
        ~CurrSource();

        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        virtual void stamp() override;
        virtual void updateStep() override;

    private:
        Pin* m_outPin;
};

#endif
