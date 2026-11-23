/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>

#include "lm2575.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuitwidget.h"
#include "e-resistor.h"
#include "e-node.h"
#include "pin.h"

#include "stringprop.h"

#define tr(str) simulideTr("Lm2575",str)

// TI LM2575 (SLVS569F, 2015), Electrical Characteristics p. 4-5 [DOC]
static const double VREF    = 1.23;     // Feedback voltage
static const double FOSC    = 52e3;     // Oscillator
static const double RON     = 0.9;      // VSAT 0.9 V at 1 A
static const double ROFF    = 1e8;      // Output leakage (2 mA max at 40 V: far less here)
static const double DMAX    = 0.98;     // Maximum duty cycle
static const double ICL     = 2.8;      // Peak current limit, typ
static const double IQ      = 5e-3;     // Quiescent current at 12 V
static const double ISTBY   = 50e-6;    // Standby
static const double VONOFF  = 1.4;      // ON/OFF threshold [INFERRED: VIL < 1.4 < VIH 2.2 V]
static const double VINMIN  = 4.75;     // Input voltage range
// Internal compensation [INFERRED]: per-cycle PID in the feedback-pin domain, normalised by the
// plant gain VIN * (VREF / Vout): integral 2π·100 Hz, proportional 0.5, derivative 0.4 ms
// (critical damping of the datasheet's 330 µH / 330 µF filter with an ideal capacitor)
static const double KI = 2*M_PI*100;
static const double KP = 0.5;
static const double KD = 4e-4;

Component* Lm2575::construct( QString type, QString id )
{ return new Lm2575( type, id ); }

LibraryItem* Lm2575::libraryItem()
{
    return new LibraryItem(
        tr("Buck Regulator"),
        "Other Active",
        "ic2.png",
        "LM2575",
        Lm2575::construct );
}

Lm2575::Lm2575( QString type, QString id )
      : PllVcoBase( type, id )
{
    m_title = "LM2575";
    m_area = QRectF(-24,-22, 48, 44 );   // Tall enough for the title, drawn upright
    setLabelPos(-24,-36, 0 );
    m_pin.resize( 5 );
    addPin( P_VIN,   "vin",   "VIN",    false, 2 );
    addPin( P_ONOFF, "onoff", "ON/OFF", false, 3 );
    addPin( P_GND,   "gnd",   "GND",    false, 4 );
    addPin( P_OUT,   "out",   "OUT",    true,  2 );
    addPin( P_FB,    "fb",    "FB",     true,  3 );
    for( Pin* p : m_pin ) p->setY( p->y()+16 );   // Rows 2..4 -> y -4..12, then centred
    for( Pin* p : m_pin ) p->setY( p->y()-12 );  // Pins at y -8, 0, 8

    setNumEpins( E_COUNT );
    m_sw = new eResistor( id+"-sw" );
    m_sw->setEpin( 0, m_pin[P_VIN] );
    m_sw->setEpin( 1, m_pin[P_OUT] );
    m_sup = new eResistor( id+"-sup" );
    m_sup->setEpin( 0, m_ePin[E_SUP1] );
    m_sup->setEpin( 1, m_ePin[E_SUP2] );

    m_vnom = 5;
    m_model = "5.0";
    addPropGroup( { tr("Main"), {
        new StrProp<Lm2575>("Model", tr("Output"), "ADJ,3.3,5.0,12,15"
                           , this, &Lm2575::model, &Lm2575::setModel, 0, "enum" ),
    },0} );
    setShowProp("Model");
    setValLabelText("5.0");
}
Lm2575::~Lm2575(){}

void Lm2575::remove()
{
    delete m_sw;
    delete m_sup;
    Component::remove();
}

void Lm2575::setModel( QString m )
{
    if( m != "ADJ" && m != "3.3" && m != "5.0" && m != "12" && m != "15" ) return;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_model = m;
    m_vnom = ( m == "ADJ" ) ? 0 : m.toDouble();
}

void Lm2575::initialize()
{
    m_pending = false;
    m_on = false;
    m_enabled = false;
    m_next = 0;
    m_cycleT = 0;
    m_duty = 0;
    m_ui = 0;
    m_lastFb = 0;
    m_lastValid = false;
    m_limited = 0;
}

void Lm2575::stamp()
{
    eNode* nVin = m_pin[P_VIN]->getEnode();
    eNode* nGnd = m_pin[P_GND]->getEnode();
    m_ePin[E_SUP1]->setEnode( nGnd ? nVin : nullptr );
    m_ePin[E_SUP2]->setEnode( nVin ? nGnd : nullptr );
    m_sup->setRes( 12/IQ );
    m_sw->setRes( ROFF );
    watch({ P_OUT });                         // Current limit while on
    Simulator::self()->addEvent( 1, this );   // First cycle
}

double Lm2575::switchCurrent()
{
    if( !m_pin[P_VIN]->isConnected() || !m_pin[P_OUT]->isConnected() ) return 0;
    return ( m_pin[P_VIN]->getVoltage()-m_pin[P_OUT]->getVoltage() )*m_sw->admit();
}

void Lm2575::setSwitch( bool on )
{
    if( on == m_on ) return;
    m_on = on;
    m_sw->setRes( on ? RON : ROFF );
}

void Lm2575::startCycle()
{
    Simulator* sim = Simulator::self();
    uint64_t period = (uint64_t)( 1e12/FOSC );
    m_cycleT = sim->circTime();
    double gnd = m_pin[P_GND]->getVoltage();
    double vin = m_pin[P_VIN]->getVoltage()-gnd;
    bool off = m_pin[P_ONOFF]->isConnected() && m_pin[P_ONOFF]->getVoltage()-gnd > VONOFF;
    m_enabled = !off && vin >= VINMIN;
    double rq = qMax( vin, 0.1 )/( off ? ISTBY : IQ );   // Supply current at this VIN
    if( qFabs( rq*m_sup->admit()-1 ) > 0.01 ) m_sup->setRes( rq );

    if( !m_enabled ){
        setSwitch( false );
        m_ui = 0; m_duty = 0; m_lastValid = false;
        m_next = 0;
        sim->addEvent( period, this );
        return;
    }
    // Error amplifier, once per cycle, in the feedback-pin domain
    double fb = m_pin[P_FB]->isConnected() ? m_pin[P_FB]->getVoltage()-gnd : 0;
    // Plant gain d(Vfb)/d(duty) = VIN * VREF/Vout: fixed models know Vout; on ADJ the divider is
    // outside, but in steady state Vout = duty * VIN, so the gain is VREF / duty
    double plant;
    if( m_vnom > 0 ){ double k = VREF/m_vnom; fb *= k; plant = vin*k; }
    else plant = VREF/qMax( m_duty, 0.1 );
    double T = 1/FOSC;
    double e = VREF-fb;
    double deriv = m_lastValid ? ( fb-m_lastFb )/T : 0;
    m_lastFb = fb;
    m_lastValid = true;
    m_ui += KI*e*T/plant;
    m_ui = qBound(-0.5, m_ui, 1.0 );
    m_duty = qBound( 0.0, m_ui+( KP*e-KD*deriv )/plant, DMAX );

    if( m_duty < 0.02 ){                       // Pulse skipped
        setSwitch( false );
        m_next = 0;
        sim->addEvent( period, this );
        return;
    }
    setSwitch( true );
    m_next = 1;
    sim->addEvent( qMax( (uint64_t)1, (uint64_t)( m_duty*period ) ), this );
}

void Lm2575::runEvent()
{
    Simulator* sim = Simulator::self();
    uint64_t period = (uint64_t)( 1e12/FOSC );
    if( m_next == 0 ){ startCycle(); return; }
    setSwitch( false );                         // End of the on time, or the current limit
    uint64_t end = m_cycleT+period;
    uint64_t now = sim->circTime();
    m_next = 0;
    sim->addEvent( end > now ? end-now : 1, this );
}

void Lm2575::stepSettled()   // Cycle-by-cycle current limit
{
    if( !m_on || m_next != 1 ) return;
    if( switchCurrent() < ICL ) return;
    m_limited++;
    m_next = 2;
    Simulator::self()->cancelEvents( this );
    Simulator::self()->addEvent( 1, this );
}

void Lm2575::simState( StateIO& s )
{
    PllVcoBase::simState( s );
    s.io( m_vnom );
    s.io( m_on );
    s.io( m_enabled );
    s.io( m_next );
    s.io( m_cycleT );
    s.io( m_duty );
    s.io( m_ui );
    s.io( m_lastFb );
    s.io( m_lastValid );
    s.io( m_limited );
}
