/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "mcutwi.h"
#include "e_mcu.h"

McuTwi::McuTwi( eMcu* mcu, QString name )
      : McuPrescaled( mcu, name )
      , TwiModule( mcu->getId()+"-"+name )
{
    m_dataReg = NULL;
    m_addrReg = NULL;
    m_statReg = NULL;
}

McuTwi::~McuTwi()
{
}

void McuTwi::initialize()
{
    TwiModule::initialize();

    m_prescaler = 1;
    updateFreq();
}

void McuTwi::simState( StateIO& s )
{
    McuPrescaled::simState( s );
    TwiModule::simState( s );
}
