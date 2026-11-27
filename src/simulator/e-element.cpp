/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "e-element.h"
#include "simstate.h"
#include "simulator.h"
#include "e-pin.h"

eElement::eElement( QString id )
{
    m_elmId = id;
    nextChanged = NULL;
    nextEvent  = NULL;
    eventTime = 0;
    m_pendingTime = 0;
    added = false;
    m_step = 0;

    if( Simulator::self() ) Simulator::self()->addToElementList( this );
}
eElement::eElement( QString id, bool listed )
{
    m_elmId = id;
    nextChanged = NULL;
    nextEvent  = NULL;
    eventTime = 0;
    m_pendingTime = 0;
    added = false;
    m_step = 0;

    if( listed && Simulator::self() ) Simulator::self()->addToElementList( this );
}
eElement::~eElement()
{
    m_ePin.clear();
    if( !Simulator::self() ) return;
    Simulator::self()->remFromElementList( this );
    Simulator::self()->cancelEvents( this );
}

void eElement::setNumEpins( int n )
{
    m_ePin.resize(n);
    for( int i=0; i<n; ++i )
    {
        if( m_ePin[i] == NULL )
            m_ePin[i] = new ePin( m_elmId+"-ePin"+QString::number(i), i );
}   }

ePin* eElement::getEpin( int num )
{ return m_ePin[num]; }

void eElement::setEpin( int num, ePin* pin )
{ m_ePin[num] = pin; }

void eElement::pauseEvents()
{
    if( eventTime )
    {
        m_pendingTime = eventTime - Simulator::self()->circTime();
        Simulator::self()->cancelEvents( this );
    }
    else m_pendingTime = 0;
}

void eElement::resumeEvents()
{
    if( !m_pendingTime ) return;
    Simulator::self()->addEvent( m_pendingTime, this );
    m_pendingTime = 0;
}

// A model that publishes its pin currents (BJT, JFET, current regulator, op amp): they are read
// later (noise sources, meters), so they belong to the state
void eElement::pinCurrents( StateIO& s )
{
    for( ePin* p : m_ePin ){
        double a = p ? p->trueCurrent() : 0;
        s.io( a );
        if( p && !s.saving() ) p->setTrueCurrent( a );
    }
}

void eElement::simState( StateIO& s )   // Base fields; the event queue is the simulator's
{
    s.io( m_changed );
    s.io( m_step );
    s.io( m_pendingTime );
}
