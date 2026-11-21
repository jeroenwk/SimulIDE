/***************************************************************************
 *   Copyright (C) 2021 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "mcuwdt.h"
#include "e_mcu.h"
#include "cpubase.h"
#include "mcuinterrupts.h"
#include "simulator.h"

McuWdt::McuWdt( eMcu* mcu, QString name )
      : McuPrescaled( mcu, name )
      , eElement( mcu->getId()+"-"+name )
{
    m_wdtFuse  = false;
}
McuWdt::~McuWdt(){}

void McuWdt::simState( StateIO& s )
{
    McuPrescaled::simState( s );
    eElement::simState( s );
    s.io( m_wdtFuse );
    s.io( m_ovfInter );
    s.io( m_ovfReset );
    s.io( m_ovfPeriod );
    s.io( m_clkPeriod );
}
