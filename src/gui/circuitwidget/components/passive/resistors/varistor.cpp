/***************************************************************************
 *   Varistor (metal-oxide, MOV): symmetric clamp, one non-linear element  *
 *   with the datasheet's power law I = 1 mA (|V|/V1mA)^alpha              *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>
#include <cmath>

#include "varistor.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "e-node.h"
#include "e-pin.h"
#include "pin.h"

#include "doubleprop.h"

#define tr(str) simulideTr("Varistor",str)

// A real MOV follows I ~ V^alpha (alpha ~ 20-40): almost nothing below its voltage, then a steep
// rise. Here exactly that power law through the two datasheet points, 1 mA at the varistor
// voltage and the maximum clamping voltage at its peak current (07D561K: 560 V, 925 V at 10 A:
// alpha = 18.35), the same in both directions, plus 10 GOhm of leakage. The datasheet clamping
// voltage is a maximum: a real part clamps a little lower. One element between the pins, no inner
// node (until 2026-11 two zener junctions in anti-series and a resistor: a straight line from
// the knee to the datasheet point, 3.8 A at 700 V where the power law gives 60 mA). Not
// modelled: the capacitance (~100 pF for 7 mm discs), energy rating, ageing.

Component* Varistor::construct( QString type, QString id )
{ return new Varistor( type, id ); }

LibraryItem* Varistor::libraryItem()
{
    return new LibraryItem(
        tr("Varistor"),
        "Resistors",
        "varistor.png",
        "Varistor",
        Varistor::construct );
}

Varistor::Varistor( QString type, QString id )
        : Comp2Pin( type, id )
        , eResistor( id )
{
    m_area = QRectF(-11,-4.5, 22, 9 );
    m_pin[0]->setLength( 5 );
    m_pin[1]->setLength( 5 );
    m_ePin[0] = m_pin[0];
    m_ePin[1] = m_pin[1];

    m_volt1mA   = 560;              // 07D561K
    m_clampVolt = 925;
    m_clampCurr = 10;
    m_maxCurr   = 1200;             // 8/20 µs peak current of 7 mm 560 V discs [INFERRED, typical]
    m_gLeak     = 1e-10;            // 10 GΩ: tens of nA at mains, below a real MOV's µA [INFERRED]
    m_volt      = 0;
    m_firstIter = true;
    m_state     = -1;
    updateValues();

    addPropGroup( { tr("Main"), {
        new DoubProp<Varistor>("Volt1mA", tr("Varistor Voltage (1 mA)"), "V"
                              , this, &Varistor::volt1mA, &Varistor::setVolt1mA ),

        new DoubProp<Varistor>("ClampVolt", tr("Max Clamping Voltage"), "V"
                              , this, &Varistor::clampVolt, &Varistor::setClampVolt ),

        new DoubProp<Varistor>("ClampCurr", tr("at Peak Current"), "A"
                              , this, &Varistor::clampCurr, &Varistor::setClampCurr ),

        new DoubProp<Varistor>("MaxCurrent", tr("Max Peak Current (8/20 µs)"), "A"
                              , this, &Varistor::maxCurrent, &Varistor::setMaxCurrent ),
        showStateProp()
    }, 0 } );
    enableStates( true );

    setShowProp("Volt1mA");
}
Varistor::~Varistor(){}

void Varistor::setVolt1mA( double v )   { if( v > 1 ) m_volt1mA = v; updateValues(); }
void Varistor::setClampVolt( double v ) { if( v > 0 ) m_clampVolt = v; updateValues(); }
void Varistor::setClampCurr( double i ) { if( i > 0 ) m_clampCurr = i; updateValues(); }

void Varistor::updateValues()
{
    // Through (V1mA, 1 mA) and (Vclamp, Iclamp); a clamping voltage at or below V1mA (or a test
    // current at or below 1 mA) has no slope to fit: keep the steepness of a typical MOV
    double alpha = 30;
    if( m_clampVolt > m_volt1mA*1.0001 && m_clampCurr > 1e-3 )
        alpha = qLn( m_clampCurr/1e-3 )/qLn( m_clampVolt/m_volt1mA );
    if( alpha < 2 )   alpha = 2;
    if( alpha > 100 ) alpha = 100;
    m_alpha = alpha;
    m_firstIter = true;
}

double Varistor::curr( double v )
{
    double a = qFabs( v )/m_volt1mA;
    double i = a > 0 ? 1e-3*qExp( m_alpha*qLn( a ) ) : 0;
    return ( v < 0 ? -i : i ) + m_gLeak*v;
}

double Varistor::cond( double v )
{
    double av = qFabs( v );
    if( av < 1e-12 ) return m_gLeak;
    double i = 1e-3*qExp( m_alpha*qLn( av/m_volt1mA ) );
    return m_alpha*i/av + m_gLeak;
}

void Varistor::initialize()
{
    m_state = -1;
    clearStates();
}

void Varistor::stamp()
{
    m_volt = 0;
    m_firstIter = true;
    m_admit = m_gLeak;
    eResistor::stamp();

    eNode* node = m_ePin[0]->getEnode();
    if( node ) node->addToNoLinList( this );
    node = m_ePin[1]->getEnode();
    if( node ) node->addToNoLinList( this );
    m_ePin[0]->createCurrent();
    m_ePin[1]->createCurrent();
}

// Newton on the power law. The current changes by e² per 2/alpha of relative voltage, so above
// the knee a step is limited to a factor e^(2/alpha) in |V| (as a diode limits to 2 Vt)
void Varistor::voltChanged()
{
    if( !m_ePin[0]->isConnected() || !m_ePin[1]->isConnected() ) return;
    double v = m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage();

    Simulator::self()->addToSettled( this );   // State from the settled step, also on a slow ramp
    if( !m_firstIter && ( qFabs( v-m_volt ) < .01 || currConverged( v ) ) ) return; // Converged
    m_firstIter = false;
    Simulator::self()->notCorverged();

    double av = qFabs( v );
    if( av > m_volt1mA )
    {
        double base = ( v*m_volt > 0 ) ? std::max( qFabs( m_volt ), m_volt1mA ) : m_volt1mA;
        double top  = base*qExp( 2/m_alpha );
        if( av > top ) v = ( v < 0 ) ? -top : top;
    }
    m_volt = v;

    double g = cond( v );
    double i = curr( v );
    m_admit = g;
    eResistor::stampAdmit();

    double stCurr = i-g*v;
    m_ePin[0]->stampCurrent(-stCurr );
    m_ePin[1]->stampCurrent( stCurr );
}

// Converged on the current too: the voltage moved 10 mV or more but the stamp (linearized at
// m_volt) predicted the element's own current there within 1 pA + 0.1 %. Below the knee the
// element is a 10 GOhm leak, linear: the voltage of a node held by little else (a floating
// mains pair near a zero crossing) may move between solves without the current changing
bool Varistor::currConverged( double v )
{
    double iLin = curr( m_volt )+m_admit*( v-m_volt );
    double iNew = curr( v );
    if( !std::isfinite( iNew ) ) return false;
    return qFabs( iNew-iLin ) <= 1e-12 + 1e-3*qMax( qFabs( iNew ), qFabs( iLin ) );
}

double Varistor::current()
{
    if( !m_ePin[0]->isConnected() || !m_ePin[1]->isConnected() ) return 0;
    return curr( m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage() );
}

// Part state (docs/PART_STATES.md): IDLE (grey: below 1 mA, the varistor voltage), CLAMPING
// (amber: conducting, at its limit), OVER (red: above its peak current rating). Hysteresis:
// back to IDLE below 0.5 mA, out of OVER below 90 % of the rating
void Varistor::stepSettled()
{
    double i = qFabs( current() );
    int st = m_state < 0 ? 0 : m_state;
    switch( st ){
        case 0: if( i >= 1e-3 ) st = ( i > m_maxCurr ) ? 2 : 1; break;
        case 1: if( i > m_maxCurr ) st = 2; else if( i < 0.5e-3 ) st = 0; break;
        case 2: if( i < 0.9*m_maxCurr ) st = ( i < 0.5e-3 ) ? 0 : 1; break;
    }
    if( st == m_state ) return;
    m_state = st;
    if     ( st == 0 ) reportState("IDLE", stOff );
    else if( st == 1 ) reportState("CLAMPING", stWait );
    else               reportState("OVER", stFault );
}

void Varistor::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    p->drawRect( m_area );
    QPen pen = p->pen();                         // The VDR mark: a diagonal with a flat foot
    pen.setWidthF( 1.2 );
    p->setPen( pen );
    p->drawLine( QPointF(-9, 7 ), QPointF(-5, 7 ) );
    p->drawLine( QPointF(-5, 7 ), QPointF( 7,-7 ) );
    Component::paintSelected( p );
}

void Varistor::simState( StateIO& s )
{
    eResistor::simState( s );
    s.io( m_volt1mA );
    s.io( m_clampVolt );
    s.io( m_clampCurr );
    s.io( m_maxCurr );
    s.io( m_alpha );
    s.io( m_gLeak );
    s.io( m_volt );
    s.io( m_firstIter );
    s.io( m_state );
}
