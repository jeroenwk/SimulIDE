/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef MCUPRESCALED_H
#define MCUPRESCALED_H


#include "simstate.h"
#include "mcumodule.h"

class McuPrescaled : public McuModule
{
        friend class McuCreator;

    public:
        void simState( StateIO& s );   // Keyframes (specs/sim-control.md §4.5)
        McuPrescaled( eMcu* mcu, QString name );
        virtual ~McuPrescaled();

        void setPrescaler( uint16_t p ) { m_prescaler = p;}
        void setPrescIndex( uint16_t p );

    protected:
        regBits_t m_prSelBits;              // Bits configuring prescaler index
        uint8_t  m_prIndex;                 // Prescaler index
        uint16_t m_prescaler;               // Actual Prescaler value
        std::vector<uint16_t> m_prescList;  // Prescaler values
};

#endif
