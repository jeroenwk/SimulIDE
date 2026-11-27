/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>
#include <limits>
#include <QPainter>

#include "volt_reg.h"
#include "connector.h"
#include "simulator.h"
#include "itemlibrary.h"
#include "pin.h"
#include "e-node.h"

#include "doubleprop.h"

#define tr(str) simulideTr("VoltReg",str)

Component* VoltReg::construct( QString type, QString id )
{ return new VoltReg( type, id ); }

LibraryItem* VoltReg::libraryItem()
{
    return new LibraryItem(
        tr("Volt. Regulator"),
        "Other Active",
        "voltreg.png",
        "VoltReg",
        VoltReg::construct );
}

static const double G_ON  = 1e6;    // Pass element on: 1 uOhm
static const double G_OFF = 1e-7;   // Off (Min_Load): 10 MOhm, 13 decades below on

VoltReg::VoltReg( QString type, QString id )
       : Component( type, id )
       , eResistor( id )
{
    m_area = QRect( -11, -8, 22, 19 );

    m_admit = G_ON;

    setValLabelPos( 15, 12, 0 );
    
    m_pin.resize( 3 );
    m_ePin.resize( 3 );

    m_ePin[0] = m_pin[0] = new Pin( 180, QPoint( -16, 0 ), id+"-input", 0, this, 5 );
    m_pin[0]->setLabelText( "I" );
    m_pin[0]->setLabelColor( QColor( 0, 0, 0 ) );

    m_ePin[1] = m_pin[1] = new Pin( 0, QPoint( 16, 0 ), id+"-output", 1, this, 5 );
    m_pin[1]->setLabelText( "O" );
    m_pin[1]->setLabelColor( QColor( 0, 0, 0 ) );

    m_ePin[2] = m_pin[2] = new Pin( 270, QPoint( 0, 16 ), id+"-ref", 2, this );
    m_pin[2]->setLength(5);
    m_pin[2]->setLabelText("R");
    m_pin[2]->setLabelColor( QColor( 0, 0, 0 ) );

    Simulator::self()->addToUpdateList( this );

    m_drop = 0.7;
    m_iAdj = 0;
    m_iMin = 0;
    m_pass = 0;

    addPropGroup( { tr("Main"), {
        new DoubProp<VoltReg>("Voltage", tr("Output Voltage"),"V"
                             , this, &VoltReg::outVolt, &VoltReg::setOutVolt ),
        new DoubProp<VoltReg>("Dropout", tr("Dropout Voltage"),"V"
                             , this, &VoltReg::dropout, &VoltReg::setDropout ),
        new DoubProp<VoltReg>("I_adj", tr("Ref Pin Current"),"A"
                             , this, &VoltReg::iAdj, &VoltReg::setIAdj ),
        new DoubProp<VoltReg>("Min_Load", tr("Minimum Load Current (0 = none)"),"A"
                             , this, &VoltReg::minLoad, &VoltReg::setMinLoad ),
        showStateProp()
    },0 } );
    enableStates( true );

    setShowProp("Voltage");
    setPropStr( "Voltage", "1.2" );
}
VoltReg::~VoltReg(){}

void VoltReg::initialize()
{
    m_region = -1;
    m_pass = 0;
    clearStates();
}

void VoltReg::stamp()
{
    m_connected = m_ePin[0]->isConnected()
               && m_ePin[1]->isConnected()
               && m_ePin[2]->isConnected();

    if( m_connected )
    {
        m_ePin[0]->getEnode()->addToNoLinList(this);
        m_ePin[1]->getEnode()->addToNoLinList(this);
        m_ePin[2]->getEnode()->addToNoLinList(this);

        m_ePin[0]->createCurrent();
        m_ePin[1]->createCurrent();
        m_ePin[2]->createCurrent();          // I_adj (may be set during a run)
    }
    m_admit = G_ON;
    m_pass = 0;
    eResistor::stamp();
    m_lastCurrent = 0;
    if( m_connected ){                      // I_adj: from the input, out of the ref pin
        double iq = ( m_vRef < 0 ? -1 : 1 )*m_iMin;   // Min_Load: input to output
        m_pin[0]->stampCurrent(-m_iAdj-iq );
        m_pin[1]->stampCurrent( iq );
        m_pin[2]->stampCurrent( m_iAdj );
    }
}

void VoltReg::updateStep()
{
    if( !m_changed ) return;
    m_changed = false;

    if( m_connected ){
        m_pin[2]->stampCurrent( m_iAdj );
        m_lastCurrent = std::numeric_limits<double>::quiet_NaN();   // Restamp the input with it
        voltChanged();
    }
}

void VoltReg::voltChanged()
{
    Simulator::self()->addToSettled( this );   // State from this step's final values
    // A negative regulator works on mirrored voltages (s = -1): its input sits below its output
    double s = ( m_vRef < 0 ) ? -1 : 1;
    double inVolt  = s*m_ePin[0]->getVoltage();
    double outVolt = s*( m_ePin[2]->getVoltage()+m_vRef );

    if( inVolt < 1e-6 ) inVolt = 0;
    double delta = inVolt-outVolt;
    if( delta < m_drop )
    {
        if( inVolt < m_drop ) delta = inVolt;
        else                  delta = m_drop;
    }
    int mode = 0;                              // Pass: 0 on, 1 off (the operating current out of
    if( m_iMin > 0 ){                          // the output), 2 off with too little headroom for it
        double vOut = s*m_ePin[1]->getVoltage();   // (create-circuit #508)
        double target = inVolt-delta;
        bool off = m_pass ? vOut > target-1e-6                    // Back on once the output falls to its setting
                          : G_ON*( inVolt-vOut-delta ) < -1e-6;  // The load would be sunk: off
        double dq = qMax( m_drop, 0.1 );
        if( off ) mode = ( inVolt-vOut < dq-( m_pass == 2 ? -1e-6 : 1e-6 ) ) ? 2 : 1;
    }
    if( mode != m_pass ){
        m_pass = mode;                         // Off: 10 MOhm; near the input the operating current
        double dq = qMax( m_drop, 0.1 );       // falls with the headroom: Min_Load*(Vin-Vout)/Dropout
        eResistor::setAdmit( mode == 0 ? G_ON : mode == 1 ? G_OFF : m_iMin/dq );
        m_lastCurrent = std::numeric_limits<double>::quiet_NaN();
    }
    double current = mode ? 0 : s*delta*m_admit;
    if( qFabs( m_lastCurrent-current ) < 1e-3 ) return;
    m_lastCurrent = current;
    Simulator::self()->notCorverged();

    double iq = ( mode == 2 ) ? 0 : s*m_iMin;  // Operating current: input to output
    m_pin[0]->stampCurrent( current-m_iAdj-iq );
    m_pin[1]->stampCurrent(-current+iq );
}

// Part state (docs/PART_STATES.md), from the model's own test: it keeps at least Dropout (0.7 V)
// between input and output, and passes nothing below Dropout of input (mirrored when Voltage < 0).
//   REGULATING  output at its set voltage (green)
//   DROPOUT     less than 0.7 V of headroom: the output follows the input down (amber)
//   OFF         input below 0.7 V: no output (grey)
//   MIN LOAD    (Min_Load > 0) the load takes less than the operating current: the pass is off and
//               the output rises above its setting (amber)
// 50 mV hysteresis on each boundary.
void VoltReg::stepSettled()
{
    if( !m_connected ) return;
    double s = ( m_vRef < 0 ) ? -1 : 1;                             // Mirrored for a negative regulator
    double inVolt = s*m_ePin[0]->getVoltage();
    double head   = inVolt-s*( m_ePin[2]->getVoltage()+m_vRef );   // Headroom past the regulated output
    double d = m_drop;
    int r = m_region;
    if( r < 0 ) r = ( inVolt < d+0.025 ) ? 0 : ( head < d+0.025 ) ? 1 : 2;
    else if( r == 0 ){ if( inVolt > d+0.05 ) r = ( head < d ) ? 1 : 2; }
    else if( inVolt < d ) r = 0;
    else if( r == 1 ){ if( head > d+0.05 ) r = 2; }
    else if( head < d ) r = 1;
    if( r >= 2 ) r = m_pass ? 3 : 2;     // Min_Load: the pass is off, the output above its setting

    if( r == m_region ) return;
    m_region = r;
    if     ( r == 0 ) reportState("OFF",        stOff );
    else if( r == 1 ) reportState("DROPOUT",    stWait );
    else if( r == 3 ) reportState("MIN LOAD",   stWait );
    else              reportState("REGULATING", stOn );
}

void VoltReg::paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
    Component::paint( p, option, widget );
    p->drawRect( m_area );

    Component::paintSelected( p );
}

void VoltReg::simState( StateIO& s )
{
    eResistor::simState( s );
    s.io( m_vRef );
    s.io( m_drop );
    s.io( m_iAdj );
    s.io( m_iMin );
    s.io( m_pass );
    s.io( m_lastCurrent );
    s.io( m_connected );
    s.io( m_region );
}
