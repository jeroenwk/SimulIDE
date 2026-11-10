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
    m_volt     = 0;
    m_prevVolt = 0;
    m_awaitSolve = false;
}
eReactive::~eReactive(){}

void eReactive::stamp()
{
    eResistor::stamp();

    if( m_ePin[0]->isConnected() && m_ePin[1]->isConnected() )
    {
        m_ePin[0]->createCurrent();
        m_ePin[1]->createCurrent();

        updtReactStep();

        m_volt = m_InitVolt;
        m_prevVolt = m_InitVolt;
        voltSample();
        m_curSource = m_InitCurr;
        m_curSource = updtCurr( m_volt, m_prevVolt );
        m_awaitSolve = true;   // BH-053: the first solved voltage is the boundary value

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
    if( m_awaitSolve )   // BH-053: first solve since stamping: this is the voltage of the
    {                    // initial condition, the one the first update has to accumulate
        m_awaitSolve = false;
        m_prevVolt = m_ePin[0]->getVoltage() - m_ePin[1]->getVoltage();
    }
    if( m_running ) return;
    m_running = true;

    Simulator::self()->addEvent( m_timeStep, this );
}

void eReactive::runEvent()
{
    double volt = m_ePin[0]->getVoltage() - m_ePin[1]->getVoltage();

    if( m_volt != volt )
    {
        // BH-053: the inductor's accumulating current source must use the voltage read at
        // the *previous* update (m_prevVolt), not the one just read: the branch current
        // during this interval is m_admit*volt + source, so accumulating the fresh sample
        // as well counts the current voltage twice whenever other elements solved in
        // between (a source, a clock...). That double count showed up as a phantom series
        // resistance of about w^2*L*reactStep (a 25 mH coil: 95 ohm at 10 kHz with the
        // default 1 us step), damped LC tanks and detuned filters. In a quiet circuit (no
        // solves between our updates) the read at this event *is* the voltage solved after
        // the previous one, which is why plain RL/RC steps were always exact.
        // The capacitor's source is the boundary voltage itself: it takes the fresh volt.
        m_curSource = updtCurr( volt, m_prevVolt );

        m_prevVolt = volt;             // the next update accumulates this interval's read
        m_volt = volt;
        voltSample();

        m_ePin[0]->stampCurrent( m_curSource );
        m_ePin[1]->stampCurrent(-m_curSource );

        Simulator::self()->addEvent( m_timeStep, this );
    }
    else m_running = false;
}

void eReactive::updtReactStep()
{
    if( m_reacStep ) m_timeStep = m_reacStep;
    else             m_timeStep = Simulator::self()->reactStep(); // Time in ps
    m_tStep = (double)m_timeStep/1e12;         // Time in seconds
    eResistor::setRes( updtRes() );

    m_running = false;
    Simulator::self()->cancelEvents( this );
}
