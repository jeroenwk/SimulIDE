/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PICMSSP_H
#define PICMSSP_H

#include "mcumodule.h"
#include "e-element.h"
#include "mcutypes.h"

class PicSpi;
class PicTwi;

class PicMssp : public McuModule, public eElement
{
    friend class McuCreator;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicMssp )
        PicMssp( eMcu* mcu, QString name, int type );
        ~PicMssp();

        virtual void initialize() override;

        virtual void configureA( uint8_t SSPCON ) override;

        //virtual void setInterrupt( Interrupt* i ) override;

    protected:
        uint8_t m_mode;

        bool m_enabled;

        regBits_t m_SSPMx;
        regBits_t m_SSPEN;

        PicSpi* m_spiUnit;
        PicTwi* m_twiUnit;
};

#endif
