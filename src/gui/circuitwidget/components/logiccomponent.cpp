/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "logiccomponent.h"
#include <algorithm>
#include <QDebug>
#include "circuitwidget.h"
#include "simulator.h"
#include "connector.h"
#include "circuit.h"
#include "iopin.h"

#define tr(str) simulideTr("LogicComponent",str)

const QString LogicComponent::m_triggerList = "None,Clock,Enable;"+tr("None")+","+tr("Clock")+","+tr("Enable");

LogicComponent::LogicComponent( QString type, QString id )
              : IoComponent( type, id )
              , eClockedDevice( id )
{
    m_oePin     = NULL;
    m_tristate  = false;
    m_openCol   = false;
    m_outEnable = true;

    m_triggerStr = "Clock";
}
LogicComponent::~LogicComponent(){}

void LogicComponent::stamp()
{
    IoComponent::initState();
    eClockedDevice::stamp();
    if( m_oePin ) m_oePin->changeCallBack( this );
    m_outEnable = true;
}

std::vector<Pin*> LogicComponent::getPins()
{
    std::vector<Pin*> pins = IoComponent::getPins();
    // BH-008: m_oePin/m_clkPin may already be in m_inPin/m_otherPin (FlipFlops, counters): list each pin once
    for( Pin* extra : { (Pin*)m_oePin, (Pin*)m_clkPin } )
    {
        if( !extra ) continue;
        if( std::find( pins.begin(), pins.end(), extra ) != pins.end() )
        { qDebug() << "[LogicComponent::getPins] BH-008 pin already listed, skipped:" << extra->pinId(); continue; }
        pins.emplace_back( extra );
    }
    return pins;
}

void LogicComponent::remove()
{
    if( m_oePin ) m_oePin->removeConnector();
    eClockedDevice::remove();
    IoComponent::remove();
}

void LogicComponent::setOePin( IoPin* pin )
{
    m_oePin = pin;
    pin->setInverted( true );
    m_oePin->setLabelText("OE ");
}

bool LogicComponent::outputEnabled()
{
    if( !m_oePin ) return true;

    double volt = m_oePin->getVoltage();
    if     ( volt > m_inHighV ) m_outEnable = false;   // Active Low
    else if( volt < m_inLowV )  m_outEnable = true;

    //m_oePin->setPinState( m_outEnable? input_low:input_high ); // Low-High colors
    return m_outEnable;
}

void LogicComponent::updateOutEnabled()
{
    if( !m_oePin ) return;

    bool outEnPrev = m_outEnable;
    bool outEn = outputEnabled();              // Refresh m_outEnable
    if( outEnPrev != outEn ) enableOutputs( outEn );
}

void LogicComponent::setTristate( bool t )  // Activate or deactivate OE Pin
{
    if( !m_oePin ) return;

    if( !t ) m_oePin->removeConnector();

    m_oePin->setVisible( t );
    m_tristate = t;
    updateOutEnabled();
}

void LogicComponent::setTriggerStr( QString t )
{
    m_triggerStr = t;

    trigger_t trigger = Clock;
    if     ( t == "None"  ) trigger = None;
    else if( t == "Clock" ) trigger = Clock;
    else if( t == "Enable") trigger = InEnable;

    setTrigger( trigger );

    if( m_showVal && (m_showProperty == "Trigger") )
        setValLabelText( t );
}

void LogicComponent::enableOutputs( bool en )
{
    for( uint i=0; i<m_outPin.size(); ++i ) m_outPin[i]->setStateZ( !en );
}

void LogicComponent::setInputHighV( double volt )
{
    Simulator::self()->pauseSim();
    IoComponent::setInputHighV( volt );
    if( m_clkPin) m_clkPin->setInputHighV( volt );
    Simulator::self()->resumeSim();
}

void LogicComponent::setInputLowV( double volt )
{
    Simulator::self()->pauseSim();
    IoComponent::setInputLowV( volt );
    if( m_clkPin) m_clkPin->setInputLowV( volt );
    Simulator::self()->resumeSim();
}

void LogicComponent::setInputImp( double imp )
{
    Simulator::self()->pauseSim();
    IoComponent::setInputImp( imp );
    if( m_clkPin) m_clkPin->setInputImp( imp );
    Simulator::self()->resumeSim();
}
