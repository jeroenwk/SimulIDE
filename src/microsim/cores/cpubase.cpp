/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "cpubase.h"

CpuBase::CpuBase( eMcu* mcu )
       : CoreBase()
{
    m_mcu = mcu;

    m_retCycles = 2;

    m_spl = NULL;
    m_sph = NULL;
    m_STATUS = NULL;
}
CpuBase::~CpuBase() {}

void CpuBase::reset()
{
    m_PC = 0;
    m_RET_ADDR = 0;     // Not the last run's (the first run after opening differed)
}

void CpuBase::coreState( StateIO& s )
{
    s.io( m_retCycles );
    s.io( m_PC );
    s.io( m_RET_ADDR );
}
