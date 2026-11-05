/***************************************************************************
 *   Copyright (C) 2020 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "oscopechannel.h"
#include "oscope.h"
#include "plotdisplay.h"
#include "datawidget.h"
#include "simulator.h"
#include "e-pin.h"
#include "utils.h"

OscopeChannel::OscopeChannel( Oscope* oscope, QString id )
             : DataChannel( oscope, id )
{
    m_oscope = oscope;
    m_filter = 0.0;
    m_trigReset = true;
    m_trigCount = 0;
    m_trigArmed = false;
    m_trigPrevData = 0;
    m_trigPrevTime = 0;
}
OscopeChannel::~OscopeChannel()  { }

void OscopeChannel::initialize()
{
    m_rising   = false;
    m_falling  = false;

    m_simTime = -1;
    m_slotStart = 0;
    m_period  = 0;
    m_risEdge = 0;
    m_nCycles = 0;
    m_totalP  = 0;
    m_numMax  = 0;
    m_lastMax = 0;
    m_ampli   = 0;
    m_maxVal = -1e12;
    m_minVal = 1e12;
    m_midVal = 0;
    m_dispMax = 5;
    m_dispMin =-5;

    m_lastValue = 0;
    m_stepData  = 0;
    m_noWave    = 0;
    m_bufferCounter = 0;

    m_freq = 0;
    m_trigReset = true;

    m_buffer.fill(0);
    m_time.fill(0);

    updateStep();
}

void OscopeChannel::updateValues()
{
    /// m_dataTime = 0;

    if( !Simulator::self()->isRunning() ) m_freq = 0;
    double val = m_freq*1e12;
    int decs = 0;
    QString unit = " ";
    if( val >= 1 ) {valToUnit( val, unit, decs );}
    QString f = " "+QString::number( val, 'f', decs )+unit+"Hz";

    m_oscope->dataW()->setData( m_channel, f );
}

void OscopeChannel::updateStep()
{
    uint64_t simTime = Simulator::self()->circTime();

    if( m_period > 10 )  // We have a wave
    {
        if( m_numMax > 0 )  // Periods measured since the last frame: their average
        {
            m_freq = 1e12*(double)m_numMax/(double)m_totalP;
            m_totalP = 0;
            m_numMax = 0;
        }
        if( m_freq < 20 )voltChanged();
    }else{
        voltChanged();
        m_freq = 0;
        if( ++m_noWave >= 10 )  // No edge for 10 frames: forget old extremes (a past transient)
        {
            m_noWave = 0;
            m_maxVal =-1e12;
            m_minVal = 1e12;
    }   }
    if( m_oscope->autoSC() == m_channel )
    {
        m_oscope->setTimePos( m_channel, 0 );
        if( m_period > 10 )
        {
            m_oscope->setTimeDiv( m_period/5 );
            m_oscope->setVoltDiv( m_channel, m_ampli/10 );
            m_oscope->setVoltPos( m_channel, -m_midVal );
    }   }
    else{
        m_dispMax = m_oscope->voltDiv( m_channel )*10;
        m_dispMin = 0;
    }
    updateValues();

    if( m_period > 10 )  // Do we still have a wave?
    {
        uint64_t psPF  = Simulator::self()->psPerFrame();
        uint64_t lost = m_period*2;
        if( lost < psPF*2 ) lost = psPF*2;

        if( simTime-m_lastMax > lost ) // Wave lost
        {
            m_freq    = 0;
            m_period  = 0;
            m_risEdge = 0;
            m_nCycles = 0;
            m_totalP  = 0;
            m_numMax  = 0;
            m_lastMax = 0;
            m_ampli   = 0;
}   }   }

void OscopeChannel::voltChanged()
{
    if( !m_connected ) return;

    /*if( ++m_dataTime > m_buffer.size() ) // Measure time taken to fill buffer
    {
        m_dataTime = 0;
        if( m_subRate > 0 )
            m_subSample = (simTime-m_dataZero)/1e3*m_subRate/m_buffer.size();
        m_dataZero = simTime;
    }
    if( m_subSample > 0 ) // Buffer should hold enought time to display
    {                     // Skip samples acordingly
        if( ++m_subStep > m_subSample ) m_subStep = 0;
        else return;
    }*/
    double data = m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage();
    uint64_t simTime = Simulator::self()->circTime();

    if( m_simTime != simTime )
    {
        // Edges, extremes and the frequency come from the value each time step settled at. A
        // step with non-linear parts (diodes switching) calls this for every iteration, and the
        // unconverged values in between can swing back and forth: counted as minima, they made
        // a 100 Hz bus read 150-175 Hz, and the trigger fire on them.
        if( m_simTime != (uint64_t)-1 ) findEdges( m_stepData, m_simTime );
        m_simTime = simTime;
        // A slowly changing voltage (an RC decay) changes on every reactive step, so storing
        // every change fills the buffer with a fraction of a second. Like a real scope, sample
        // at a rate set by the timebase: at most one slot per 1/50000 of the screen. Changes
        // closer together update the current slot (latest value), so the buffer holds about
        // 12 screens whatever the timebase.
        uint64_t minDt = m_oscope->timeDiv()*10/50000;
        if( m_bufferCounter == 0 && m_time[0] == 0 ) m_slotStart = simTime;   // First sample
        else if( simTime-m_slotStart >= minDt )
        {
            if( ++m_bufferCounter >= m_buffer.size() ) m_bufferCounter = 0;
            m_slotStart = simTime;
        }
    }
    m_stepData = data;
    m_buffer[m_bufferCounter] = data;
    m_time[m_bufferCounter] = simTime;
}

void OscopeChannel::findEdges( double data, uint64_t simTime )
{
    // A rising edge is a crossing of the middle level with hysteresis, like a scope's trigger:
    // armed once the signal is below mid - h, fired when it then goes above mid + h, with h 15 %
    // of the last cycle's amplitude (at least the noise filter). Counting local minima instead
    // made any small wiggle a cycle: a 0.5 V step at a rectifier's zero crossing read 150 Hz.
    if( m_trigger && !m_oscope->trigAuto() ) findTrigEdge( data, simTime );

    if( data > m_maxVal ) m_maxVal = data;
    if( data < m_minVal ) m_minVal = data;

    double ampli = m_ampli;
    double mid   = m_midVal;
    if( ampli <= 0 ){                           // No full cycle yet: what has been seen so far
        ampli = m_maxVal-m_minVal;
        mid   = m_minVal+ampli/2;
    }
    if( ampli < 2*m_filter ) return;            // Flat, or only noise

    double hyst = ampli*0.15;
    if( hyst < m_filter ) hyst = m_filter;

    if( data < mid-hyst ) m_falling = true;     // Armed
    else if( m_falling && data > mid+hyst )     // Rising edge
    {
        m_falling = false;
        if( m_risEdge > 0 )
        {
            m_period  = simTime-m_risEdge;
            m_totalP += m_period;
            m_numMax++;                         // Periods measured since the last frame
        }
        m_risEdge = simTime;
        m_lastMax = simTime;

        m_ampli   = m_maxVal-m_minVal;          // This cycle's extremes set the next thresholds
        m_midVal  = m_minVal+m_ampli/2;
        m_dispMax = m_maxVal;
        m_dispMin = m_minVal;
        m_maxVal  =-1e12;
        m_minVal  = 1e12;
}   }

void OscopeChannel::findTrigEdge( double data, uint64_t simTime )
{
    // A rising edge at a fixed level, as on a real scope: armed once the signal is below
    // level - hysteresis (the noise filter), fired when it reaches the level. The crossing time
    // is interpolated between the two settled steps around it.
    static const bool dbg = qEnvironmentVariableIsSet("SIMULIDE_SCOPE_DEBUG");

    if( m_trigReset ){
        m_trigReset = false;
        m_trigCount = 0;
        m_trigArmed = false;
        m_trigPrevTime = simTime;
        m_trigPrevData = data;
        if( dbg ) qDebug() << "[ScopeTrig] ch" << m_channel << "edges cleared at" << simTime*1e-12 << "s";
    }
    double level = m_oscope->trigLevel();
    double hyst  = m_filter > 0 ? m_filter : 1e-9;

    if( data < level-hyst )
    {
        if( dbg && !m_trigArmed ) qDebug() << "[ScopeTrig] ch" << m_channel << "armed at" << simTime*1e-12 << "s" << data << "V";
        m_trigArmed = true;
    }
    else if( m_trigArmed && data >= level )
    {
        m_trigArmed = false;
        uint64_t edge = simTime;
        if( m_trigPrevData < level && simTime > m_trigPrevTime )
            edge = m_trigPrevTime + (uint64_t)( (level-m_trigPrevData)/(data-m_trigPrevData)*(double)(simTime-m_trigPrevTime) );

        uint64_t minGap = m_oscope->timeDiv()*10/16;   // Keep the ring spanning 4 screens or more
        uint64_t last = m_trigCount ? m_trigEdges[(m_trigCount-1)%TRIG_EDGES] : 0;
        if( m_trigCount == 0 || edge-last >= minGap )
        {
            m_trigEdges[m_trigCount%TRIG_EDGES] = edge;
            m_trigCount++;
            if( m_trigCount >= 2*TRIG_EDGES ) m_trigCount -= TRIG_EDGES; // No overflow, same slots
            if( dbg ) qDebug() << "[ScopeTrig] ch" << m_channel << "edge at" << edge*1e-12 << "s level" << level << "V";
        }
    }
    m_trigPrevData = data;
    m_trigPrevTime = simTime;
}

bool OscopeChannel::trigEdgeBefore( uint64_t t, uint64_t* edge )
{
    if( m_trigReset ) return false;
    int n = m_trigCount < TRIG_EDGES ? m_trigCount : TRIG_EDGES;
    for( int i=1; i<=n; ++i )                   // Newest first
    {
        uint64_t e = m_trigEdges[(m_trigCount-i)%TRIG_EDGES];
        if( e <= t ){ *edge = e; return true; }
    }
    return false;
}

void OscopeChannel::setFilter( double f )
{
    m_risEdge = 0;
    m_nCycles = 0;
    m_totalP  = 0;
    m_numMax  = 0;

    m_filter = f;
}
