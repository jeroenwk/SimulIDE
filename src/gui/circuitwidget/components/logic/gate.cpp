/***************************************************************************
 *   Copyright (C) 2010 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include "gate.h"
#include "connector.h"
#include "circuit.h"
#include "iopin.h"
#include "simulator.h"

#include "boolprop.h"

#define tr(str) simulideTr("Gate",str)

Gate::Gate( QString type, QString id, int inputs )
    : LogicComponent( type, id )
{
    m_width = 2;
    m_initState = false;
    m_minInputs = inputs;

    setNumOuts( 1, "", 0, false );
    setNumInputs( inputs );  // Create Input Pins

    /// m_rndPD = true; // Randomize Propagation Delay:
}
Gate::~Gate(){}

QList<ComProperty*> Gate::outputProps()
{
    QList<ComProperty*> outProps = IoComponent::outputProps();
    outProps.append(
new BoolProp<Gate>( "initHigh", tr("Initial High State"),""
                  , this, &Gate::initHigh, &Gate::setInitHigh ) );

    return outProps;
}

void Gate::stamp()
{
    LogicComponent::stamp();
    for( uint i=0; i<m_inPin.size(); ++i ) m_inPin[i]->changeCallBack( this );

    int rs = Simulator::self()->randomStart( this );  // Noise on: a random start, as at power-up
    setDelayJitter( Simulator::self()->noiseSeed( this, "delay" ) );
    bool init = ( rs < 0 ) ? m_initState : rs;
    m_outPin[0]->setOutState( init );

    m_nextOutVal = m_outValue = init;

    m_settling = true;
    Simulator::self()->addToStartSettle( this );
}

void Gate::startSettled()   // Start: follow the inputs now, with no delay (gates in turn)
{
    m_settling = false;
    if( m_tristate ) LogicComponent::updateOutEnabled();

    int inputs = 0;
    for( uint i=0; i<m_inPin.size(); ++i ) if( m_inPin[i]->getInpState() ) inputs++;
    m_nextOutVal = calcOutput( inputs )? 1:0;
    if( m_nextOutVal == m_outValue ) return;
    m_outValue = m_nextOutVal;
    m_outPin[0]->setOutState( m_outValue );
}

void Gate::voltChanged()
{
    if( m_settling ) return;   // Its turn in the start settle comes
    if( m_tristate ) LogicComponent::updateOutEnabled();

    int inputs = 0;

    for( uint i=0; i<m_inPin.size(); ++i )
    {
        bool state = m_inPin[i]->getInpState();
        if( state ) inputs++;
    }
    bool out = calcOutput( inputs ); // In each gate type

    m_nextOutVal = out? 1:0;

    // Inertial delay, as a real gate: an input change that is undone before the output
    // follows it is dropped, not passed on as a pulse. A pulse that a gate sends around a
    // loop of gates kept circulating at the gate delay (create-circuit #213, #206)
    if( m_nextOutVal == m_outValue && !m_outQueue.empty() ){ cancelOutPuts( this ); return; }

    // An input barely past its threshold (a gate's own output pushing its input through a
    // feedback resistor) is answered 1 ps after one driven well past it: a real gate is slower
    // near its threshold, and the stronger change (the other gate's edge) comes first
    if( m_outQueue.empty() ? m_nextOutVal == m_outValue : m_nextOutVal == m_outQueue.back() ) return;

    double overdrive = 1e9;          // How far the inputs are past their thresholds (V)
    for( uint i=0; i<m_inPin.size(); ++i )
    {
        double d = m_inPin[i]->inpOverdrive();
        if( d < overdrive ) overdrive = d;
    }
    scheduleOutPuts( this, ( overdrive < 0.25 ) ? 1 : 0 );
}

bool Gate::calcOutput( int inputs )
{
    return ((uint)inputs == m_inPin.size()); // Default for: Buffer, Inverter, And, Nand
}

void Gate::setNumInputs( int inputs )
{
    if( inputs < m_minInputs ) return;
    IoComponent::setNumInps( inputs, "" );
    m_outPin[0]->setY( 0 );
    m_area = QRect( -11, -4*m_inPin.size(), 19, 4*2*m_inPin.size() );
}

void Gate::simState( StateIO& s )
{
    LogicComponent::simState( s );
    s.io( m_initState );
    s.io( m_settling );
    s.io( m_minInputs );
}
