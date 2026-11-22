/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef USARTRX_H
#define USARTRX_H

#include <queue>

#include "usartmodule.h"

class UartRx : public UartTR
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( UartRx )
        UartRx( UsartModule* usart, eMcu* mcu, QString name );
        ~UartRx();

        virtual void enable( uint8_t en ) override;
        virtual void voltChanged() override;
        virtual void runEvent() override;
        virtual uint8_t getData() override;

        void ignoreData( bool i ) {m_ignoreData = i; }
        void setFifoSize( uint8_t s ) { m_fifoSize = s; }

        void injectByte( uint8_t data ); // Receive a complete frame (command line interface)

    protected:
        void setRxFlags();
        void readBit();
        void rxEnd();
        void byteReceived( uint16_t frame );

        bool m_startHigh;
        bool m_ignoreData;

        uint16_t m_fifo[2];
        int  m_fifoP;
        int  m_fifoSize;
};

#endif
