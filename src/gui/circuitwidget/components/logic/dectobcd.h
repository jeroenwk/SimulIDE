/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef DECTOBCD_H
#define DECTOBCD_H

#include "logiccomponent.h"

class LibraryItem;

class DecToBcd : public LogicComponent
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( DecToBcd )
        DecToBcd( QString type, QString id );
        ~DecToBcd();

        static Component* construct( QString type, QString id );
        static LibraryItem* libraryItem();

        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void runEvent() override { IoComponent::runOutputs(); }

        bool is16Bits() { return m_16Bits; }
        void set16bits( bool set );

    private:
        bool m_16Bits;
        int  m_bits;
};

#endif

