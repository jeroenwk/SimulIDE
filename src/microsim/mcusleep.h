/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef MCUSLEEP_H
#define MCUSLEEP_H

#include "mcumodule.h"
#include "e-element.h"

class McuSleep : public McuModule, public eElement
{
        friend class McuCreator;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( McuSleep )
        McuSleep( eMcu* mcu, QString name );
        ~McuSleep();

        inline uint8_t mode() { return m_sleepMode; }

        bool enabled() { return m_enabled; }
        //virtual void sleep(){;}
        virtual void callBack() override;

    protected:
        bool m_enabled;

        //uint8_t m_sleepMode; /// in McuModule
};
#endif
