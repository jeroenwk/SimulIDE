/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef I2CTOPARALLEL_H
#define I2CTOPARALLEL_H

#include "twimodule.h"
#include "iocomponent.h"

class LibraryItem;

class I2CToParallel : public IoComponent, public TwiModule
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( I2CToParallel )
        I2CToParallel( QString type, QString id );
        ~I2CToParallel();

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void writeByte() override;
        virtual void readByte() override;
        virtual void startWrite() override;

    protected:
        IoPin* m_int;

        uint8_t m_portState;
};

#endif
