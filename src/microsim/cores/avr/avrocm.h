/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef AVROCM_H
#define AVROCM_H

#include "mcuocm.h"

class AvrOcm : public McuOcm
{
    friend class McuCreator;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( AvrOcm )
        AvrOcm( eMcu* mcu, QString name);
        ~AvrOcm();

        virtual void configureA( uint8_t newVal ) override;

    protected:
        virtual void OutputOcm() override;
};

#endif
