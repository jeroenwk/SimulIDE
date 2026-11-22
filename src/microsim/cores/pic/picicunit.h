/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef PICICUNIT_H
#define PICICUNIT_H

#include "mcuicunit.h"


class PicIcUnit : public McuIcUnit
{
    friend class PicCcpUnit;

    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( PicIcUnit )
        PicIcUnit( eMcu* mcu, QString name );
        virtual ~PicIcUnit();

        virtual void configure( uint8_t CCPxM ) override;
};

#endif
