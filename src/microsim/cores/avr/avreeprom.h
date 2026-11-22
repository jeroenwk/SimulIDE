/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef AVREEPROM_H
#define AVREEPROM_H

#include "mcueeprom.h"

class AvrEeprom : public McuEeprom
{
        friend class McuCreator;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( AvrEeprom )
        AvrEeprom( eMcu* mcu, QString name );
        ~AvrEeprom();

        virtual void initialize() override;
        virtual void runEvent() override;

        virtual void configureA( uint8_t newEECR ) override;

        virtual void writeEeprom() override;

    private:
        uint64_t m_nextCycle;

        uint8_t m_mode;

        uint8_t* m_EECR;

        regBits_t m_EEPM;
        regBits_t m_EEMPE;
        regBits_t m_EEPE;
        regBits_t m_EERE;
};

#endif
