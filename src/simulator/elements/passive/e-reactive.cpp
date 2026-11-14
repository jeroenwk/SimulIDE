/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "e-reactive.h"
#include "e-pin.h"
#include "e-node.h"
#include "simulator.h"

eReactive::eReactive( QString id )
         : eResistor( id )
{
    m_value    = 0;
    m_reacStep = 0;
    m_InitCurr = 0;
    m_InitVolt = 0;
}
eReactive::~eReactive(){}

void eReactive::stamp()
{
    m_fineStep = 0;
    eResistor::stamp();

    if( m_ePin[0]->isConnected() && m_ePin[1]->isConnected() )
    {
        m_ePin[0]->createCurrent();
        m_ePin[1]->createCurrent();

        updtReactStep();

        m_volt = m_InitVolt;
        voltSample();
        m_curSource = m_InitCurr;
        m_curSource = updtCurr();

        if( m_curSource )
        {
            m_ePin[0]->stampCurrent( m_curSource );
            m_ePin[1]->stampCurrent(-m_curSource );
        }
        m_ePin[0]->changeCallBack( this );
        m_ePin[1]->changeCallBack( this );
    }
    m_running = false;
}

void eReactive::voltChanged()
{
    if( m_running ) return;
    m_running = true;

    Simulator::self()->addEvent( m_timeStep, this );
}

void eReactive::runEvent()
{
    double volt = m_ePin[0]->getVoltage() - m_ePin[1]->getVoltage();

    if( m_volt != volt )
    {
        m_volt = volt;
        voltSample();

        m_curSource = updtCurr();

        m_ePin[0]->stampCurrent( m_curSource );
        m_ePin[1]->stampCurrent(-m_curSource );

        Simulator::self()->addEvent( m_timeStep, this );
    }
    else m_running = false;
}

uint64_t eReactive::baseStep()
{
    uint64_t step = m_reacStep ? m_reacStep : Simulator::self()->reactStep(); // Time in ps
    if( m_fineStep && m_fineStep < step ) step = m_fineStep;
    return step;
}

void eReactive::setFineStep( uint64_t ps )
{
    if( !m_ePin[0]->isConnected() || !m_ePin[1]->isConnected() ) return;
    m_fineStep = ps;
    uint64_t step = baseStep();
    if( step == m_timeStep ) return;

    double oldAdmit = m_admit;
    m_timeStep = step;
    m_tStep = (double)m_timeStep/1e12;
    eResistor::setRes( updtRes() );
    if( oldAdmit > 0 ) rescaleSource( m_admit/oldAdmit );
    m_ePin[0]->stampCurrent( m_curSource );
    m_ePin[1]->stampCurrent(-m_curSource );

    if( m_running ){                       // Next sample at the new step
        Simulator::self()->cancelEvents( this );
        Simulator::self()->addEvent( m_timeStep, this );
    }
}

void eReactive::updtReactStep()
{
    m_timeStep = baseStep();
    m_tStep = (double)m_timeStep/1e12;         // Time in seconds
    eResistor::setRes( updtRes() );

    m_running = false;
    Simulator::self()->cancelEvents( this );
}
