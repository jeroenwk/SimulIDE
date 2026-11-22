/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "lachannel.h"
#include "tunnel.h"
#include "plotdisplay.h"
#include "simulator.h"
#include "pin.h"
#include "utils.h"

LaChannel::LaChannel( LAnalizer* la, QString id )
         : DataChannel( la, id )
{
    m_analizer = la;
    m_lastCond = C_NONE;
}
LaChannel::~LaChannel(){ }

void LaChannel::initialize()
{
    m_rising  = false;
    m_risEdge = 0;
    m_busValue = 0;

    m_bufferCounter = 0;
    m_buffer.fill(0);
    m_time.fill(0);

    updateStep();

    if( !Simulator::self()->isRunning() ) m_busNodes.clear();
}
void LaChannel::stamp()    // Called at Simulation Start
{
    DataChannel::stamp();
    m_analizer->conditonMet( m_channel, C_LOW );
    addReading( 0 );

    // A channel joined by its tunnel name (no wire): listen from the start. LAnalizer::updateStep
    // did it at a screen update, so when it began listening depended on when frames fell, and a
    // replay after going back listened earlier than the first pass (16F887 CCP example)
    if( !m_pin->isBus() && !m_pin->connector() && !m_chTunnel.isEmpty() )
    {
        eNode* enode = Tunnel::getEnode( m_chTunnel );
        if( enode ){
            m_pin->setEnode( enode );
            enode->voltChangedCallback( this );
        }
    }

    if( m_pin->isBus() )
    {
        bool connected = m_pin->connector();
        m_plotBase->display()->connectChannel( m_channel, connected );

        for( eNode* node : m_busNodes )
            node->voltChangedCallback( this );
    }
}

/*void LaChannel::updateStep()
{
    //int counter = m_bufferCounter;
    //voltChanged();
    //if( counter == m_bufferCounter ) addReading( m_buffer[m_bufferCounter] );
    //double dispMax = m_analizer->voltDiv()*10;
    //double dispMin = 0;
    //m_analizer->display()->setLimits( m_channel, dispMax, dispMin );
}*/

void LaChannel::setPin( Pin* p ) { m_ePin[0] = m_pin = p; }

void LaChannel::setIsBus( bool b )
{
    m_pin->removeConnector();
    m_pin->setIsBus( b );
    m_pin->setDataChannel( b? this : NULL );
}

void LaChannel::registerEnode( eNode* enode, int n )
{
    m_busNodes[n] = enode;
}

void LaChannel::addReading( double v )
{
    uint64_t simTime = Simulator::self()->circTime();
    if( ++m_bufferCounter >= m_buffer.size() ) m_bufferCounter = 0;
    m_buffer[m_bufferCounter] = v;
    m_time[m_bufferCounter] = simTime;
}

void LaChannel::voltChanged()
{
    if( !m_connected ) return;
    uint64_t simTime = Simulator::self()->circTime();

    if( m_pin->isBus() )
    {
        double busValue = 0;
        for( int n : m_busNodes.keys() )
        {
            eNode* nod = m_busNodes.value( n );
            double volt = nod->getVolt();
            bool high = volt > m_analizer->thresholdR();
            if( high ) busValue += pow( 2, n );
        }
        if( m_busValue != busValue ){
            m_busValue = busValue;
            addReading( busValue );
        }
    }else{
        double volt = m_ePin[0]->getVoltage();

        if( volt > m_analizer->thresholdR() ) // High
        {
            if( !m_rising )     // Rising Edge
            {
                addReading( 1 );
                m_rising = true;
                m_risEdge = simTime;

                if( m_pauseOnCond )
                    m_analizer->conditonMet( m_channel, C_RISING );
            }
            if( m_pauseOnCond && (m_lastCond != C_HIGH) )
            {
                m_analizer->conditonMet( m_channel, C_HIGH );
                m_lastCond = C_HIGH;
        }   }
        else if( volt < m_analizer->thresholdF() )   // Low
        {
            if( m_rising  )    // Falling Edge
            {
                addReading( 0 );
                m_rising = false;

                if( m_pauseOnCond )
                    m_analizer->conditonMet( m_channel, C_FALLING );
            }
            if( m_pauseOnCond && (m_lastCond != C_LOW) )
            {
                m_analizer->conditonMet( m_channel, C_LOW );
                m_lastCond = C_LOW;
        }   }
    }
}

void LaChannel::simState( StateIO& s )   // A display, as the scope channel: not in a keyframe
{
    DataChannel::simState( s );
}

void LaChannel::stateRestored()            // The trace cut back to the restored time
{
    uint64_t now = Simulator::self()->circTime();
    int size = m_buffer.size();
    int i = m_bufferCounter;
    for( int n=0; n<size && m_time[i] > now; ++n )
    {
        m_time[i] = 0;
        m_buffer[i] = 0;
        if( --i < 0 ) i = size-1;
    }
    m_bufferCounter = i;
}
