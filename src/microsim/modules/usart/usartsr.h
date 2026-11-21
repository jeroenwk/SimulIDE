/***************************************************************************
 *   Copyright (C) 2025 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef USARTSR_H
#define USARTSR_H

#include "usartmodule.h"

class UartSr : public UartTR
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( UartSr )
        UartSr( UsartModule* usart, eMcu* mcu, QString name );
        ~UartSr();

        virtual void enable( uint8_t en ) override;
        virtual void runEvent() override;

        void processData( uint8_t data );
        void startTransmission();

    protected:

        IoPin* m_clkPin;
};

#endif
