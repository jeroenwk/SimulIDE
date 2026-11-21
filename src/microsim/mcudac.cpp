/***************************************************************************
 *   Copyright (C) 2023 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "mcudac.h"
#include "e_mcu.h"

McuDac::McuDac( eMcu* mcu, QString name )
      : McuModule( mcu, name )
      , eElement( mcu->getId()+"-"+name )
{
    m_dacReg = NULL;

    m_pRefPin = NULL;
    m_nRefPin = NULL;
    m_outPin  = NULL;
}
McuDac::~McuDac(){}

void McuDac::initialize()
{
    m_enabled   = false;
    m_outVoltEn = false;

    m_outVal = 0;
    m_vRefP = 0;
    m_vRefN = 0;
}

void McuDac::simState( StateIO& s )
{
    McuModule::simState( s );
    eElement::simState( s );
    s.io( m_enabled );
    s.io( m_outVoltEn );
    s.io( m_outVolt );
    s.io( m_vRefP );
    s.io( m_vRefN );
    s.io( m_outVal );
}
