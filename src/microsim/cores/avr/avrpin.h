/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef AVRPIN_H
#define AVRPIN_H

#include "mcupin.h"

class Component;
class McuPort;
class eMcu;

class AvrPin : public McuPin
{
    friend class McuPort;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( AvrPin )
        AvrPin( McuPort* port, int i, QString id , Component* mcu );
        ~AvrPin();

        virtual void setPortState( bool state ) override;
        virtual void ConfExtInt( uint8_t bits ) override;

    protected:

};

#endif
