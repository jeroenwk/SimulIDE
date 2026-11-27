/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>

#include <QPainter>
#include <QMenu>

#include "diode.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("Diode",str)

Component* Diode::construct( QString type, QString id )
{ return new Diode( type, id ); }

LibraryItem* Diode::libraryItem()
{
    return new LibraryItem(
        tr("Diode"),
        "Rectifiers",
        "diode.png",
        "Diode",
        Diode::construct);
}

Diode::Diode( QString type, QString id, bool zener )
     : LinkerComponent( type, id )
     , eDiode( id )
{
    m_area = QRect(-10, -8, 20, 16 );

    m_pin.resize(2);
    m_pin[0] = new Pin( 180, QPoint(-16, 0 ), id+"-lPin", 0, this);
    m_pin[1] = new Pin( 0,   QPoint( 16, 0 ), id+"-rPin", 1, this);

    setValLabelPos(-16, 6, 0 );
    setLabelPos(-16,-24, 0 );

    // Pin0--eDiode--ePin1--midEnode--ePin2--eResistor--Pin1
    m_ePin[0] = m_pin[0];
    setNumEpins( 3 );

    m_resistor = new eResistor( m_elmId+"-resistor");
    m_resistor->setEpin( 0, m_ePin[2] );
    m_resistor->setEpin( 1, m_pin[1] );

    m_isZener = zener;
    m_surgeCur = zener ? 0 : 30;   // A 1 A rectifier's IFSM (1N4007, ES1J: 30 A for 8.3 ms). A
                                   // zener's surge rating is a reverse power: none by default
    m_absCur = m_signedCur = m_avgCur = m_peakCur = 0;
    m_lastT = m_surgeT = 0;
    m_surged = false;
    if( zener ){
        m_diodeType = "zener";
        setModel( "Zener Default" );
    }else{
        m_diodeType = "diode";
        setModel( "Diode Default" );
    }
    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new StrProp<Diode>("Model", tr("Model"), m_diodes.keys().join(",")
                          , this, &Diode::model, &Diode::setModel, 0,"enum" ),
        showStateProp()
    },0} );
    enableStates( zener );   // Zeners show their state, diodes only on request (many in a circuit)
    setShowProp("Model");    // the model as its value label, as a Resistor its resistance (#749)
    setValLabelText( model() );

    addPropGroup( { tr("Electric"), {
        new DoubProp<Diode>("Threshold" , tr("Forward Voltage"),"V"
                           , this, &Diode::threshold, &Diode::setThreshold ),

        new DoubProp<Diode>("MaxCurrent", tr("Max Average Current (IF(AV))"),"A"
                           , this, &Diode::maxCurrent, &Diode::setMaxCurrent ),

        new DoubProp<Diode>("SurgeCurrent", tr("Surge Current (IFSM, 0 = none)"),"A"
                           , this, &Diode::surgeCurrent, &Diode::setSurgeCurrent ),

        new DoubProp<Diode>("Resistance", tr("Resistance"),"Ω"
                           , this, &Diode::res, &Diode::setResSafe ),
    },0} );

    addPropGroup( { tr("Advanced"), {
        new DoubProp<Diode>("BrkDownV", tr("Breakdown Voltage"),"V"
                           , this, &Diode::brkDownV, &Diode::setBrkDownV ),

        new DoubProp<Diode>("SatCurrent", tr("Saturation Current"),"nA"
                           , this, &Diode::satCur, &Diode::setSatCur ),

        new DoubProp<Diode>("EmCoef", tr("Emission Coefficient"),""
                           , this, &Diode::emCoef, &Diode::setEmCoef )
    },0} );

    addPropGroup( { tr("Capacitance"), {
        new DoubProp<Diode>("Cj0", tr("Junction Capacitance at 0 V"), "pF"
                          , this, &Diode::cj0, &Diode::setCj0 ),
        new DoubProp<Diode>("Vj", tr("Junction Potential"), "V"
                          , this, &Diode::vj, &Diode::setVj ),
        new DoubProp<Diode>("Mj", tr("Grading Coefficient"), ""
                          , this, &Diode::mj, &Diode::setMj )
    },0} );
}
Diode::~Diode()
{
    delete m_resistor;
}

bool Diode::setPropStr( QString prop, QString val )
{
    if( prop =="Zener_Volt" ) //  Old: TODELETE
    {
        double zenerV = val.toDouble();
        m_isZener = zenerV > 0;
        eDiode::setBrkDownV( zenerV );
    }
    else{
        bool ok = Component::setPropStr( prop, val );
        // The value label shows the model (create-circuit #749): a string property does not
        // refresh it, and a parameter set by hand turns the model Custom
        if( m_showVal && m_showProperty == "Model" ) setValLabelText( model() );
        return ok;
    }
    return true;
}

void Diode::initialize()
{
    m_crashed = false;
    m_warning = false;

    m_midEnode = new eNode( m_elmId+"-mideNode");
    eDiode::initialize();
    m_region = -1;
    clearStates();
    m_absCur = m_signedCur = m_avgCur = m_peakCur = 0;
    m_lastT = m_surgeT = 0;
    m_surged = false;

    update();
}

void Diode::stamp()
{
    m_ePin[1]->setEnode( m_midEnode );
    m_ePin[2]->setEnode( m_midEnode );

    eDiode::stamp();
}

// Ratings as in a datasheet. Max Current is IF(AV), the average a rectifier carries for good: it is
// compared with |I| through a first-order low-pass of 100 ms (thermal-like: a 50/60 Hz ripple is
// smoothed, a 10 ms inrush of 5 A reaches 0.5 A). Surge Current is IFSM, the non-repetitive peak
// (30 A for an 8.3 ms half-sine on a 1 A rectifier): compared with the current of each solved
// step. Before, the instantaneous current of the frame was compared with IF(AV): the bridge of
// led_driver board_mains flashed on its 1.9 A peaks while it averaged 27 mA.
// Flags only: the diode is not damaged, the circuit goes on as before.
#define AVG_TAU    100e9   // ps
#define SURGE_HOLD 100e9   // ps: SURGE shown this long after the last peak above IFSM

// Called for every diode on every solved step (54 million times in 450 ms of led_driver
// board_mains): kept to a few multiplies, no division, no virtual call (+3 % at first)
inline void Diode::average( uint64_t t )   // |I| held since m_lastT, into the low-pass
{
    if( t <= m_lastT ) return;
    double x = (double)(t - m_lastT)*( 1/AVG_TAU );
    double k = ( x < 0.01 ) ? x : 1-std::exp( -x );   // Steps are far shorter than tau: no exp()
    m_avgCur += ( m_absCur - m_avgCur )*k;
    m_lastT = t;
}

double Diode::avgAt( uint64_t t )
{
    if( t <= m_lastT ) return m_avgCur;
    double x = (double)(t - m_lastT)*( 1/AVG_TAU );
    double k = ( x < 0.01 ) ? x : 1-std::exp( -x );
    return m_avgCur + ( m_absCur - m_avgCur )*k;
}

double Diode::avgCurrent() { return avgAt( Simulator::self()->circTime() ); }

// step: at a solved step (average( t ) done, avg = m_avgCur); else a GUI frame, which only reads
void Diode::setRegion( uint64_t t, double avg, bool step )
{
    if( m_surgeCur > 0 && m_absCur > m_surgeCur ){ m_surged = true; if( step ) m_surgeT = t; }  // Still above
    else if( m_surged && t - m_surgeT > SURGE_HOLD ) m_surged = false;

    double ai = m_absCur;
    int r = m_region;
    if( m_surged ) r = 4;
    else if( m_maxCur > 0 && ( r == 3 ? avg > 0.95*m_maxCur : avg > m_maxCur ) ) r = 3;
    else{
        // Thresholds above the saturation current: reverse-biased, a junction leaks Is (3 uA for
        // the ES1J fitted to led_driver's D3A), which is not conduction nor breakdown
        bool on = ( r == 1 || r == 2 || r == 3 || r == 4 ) ? ai > 1e-6 + 2*m_satCur
                                                           : ai > 1e-5 + 10*m_satCur;
        if( !on ) r = 0;
        else      r = ( m_signedCur > 0 ) ? 1 : 2;
    }
    if( r == m_region ) return;
    m_region = r;
    if     ( r == 0 ) reportState("OFF", stOff );
    else if( r == 1 ) reportState( m_isZener ? "FORWARD" : "ON", stWait );
    else if( r == 2 ){
        if( m_isZener ) reportState("REGULATING", stOn );
        else            reportState("BREAKDOWN",  stFault );
    }
    else if( r == 3 ) reportState("OVER", stFault );
    else              reportState("SURGE", stFault );
}

void Diode::updateStep()   // GUI thread, worker idle
{
    // Nothing changed since the last solved step (a DC circuit): the average still moves and
    // SURGE still ends, so the state is updated here too, with the current held since then
    uint64_t t = Simulator::self()->circTime();
    setRegion( t, avgAt( t ), false );   // Reads only: the stored average follows the solved steps

    bool warning = m_maxCur > 0 && m_avgCur > m_maxCur;
    bool crashed = m_surged || ( m_maxCur > 0 && m_avgCur > 2*m_maxCur );
    if( warning || crashed || m_warning || m_crashed ) update();
    m_warning = warning || crashed;
    m_crashed = crashed;

    if( m_changed ) voltChanged(); // m_changed cleared at eDiode::voltChanged
}

void Diode::voltChanged()
{
    Simulator::self()->addToSettled( this );   // State from this step's final values
    eDiode::voltChanged();
    if( !m_converged ) return;

    if( !m_linkedComp.isEmpty() )
    {
        double current = m_resistor->current();
        for( Component* comp : m_linkedComp ) comp->setLinkedValue( current );
    }
}

// Part state (docs/PART_STATES.md), from the junction current (forward positive):
//   OFF         less than 10 uA + 10 Is either way (back below 1 uA + 2 Is: a factor 10, near zero
//               currents ring; Is: a reverse-biased junction leaks its saturation current)
//   ON          conducting forward (amber, a closed switch). A zener: FORWARD
//   BREAKDOWN   conducting in reverse (the junction voltage is negative): red for a diode,
//               REGULATING (green) for a zener, whose working state it is
//   OVER        averaged current (100 ms low-pass) above Max Current, IF(AV) (back below 0.95 x): red
//   SURGE       a solved step above Surge Current, IFSM; held 100 ms after the last one: red
// Current, not voltage: the forward voltage at 10 uA..1 A depends on the model (Is, n).
void Diode::stepSettled()   // Worker: a solved step
{
    double i  = junctionCurrent();   // Anode to cathode, without the junction capacitance's current
    double ai = qFabs( i );          // (a reverse-biased diode charging its Cj is not in breakdown)
    uint64_t t = Simulator::self()->circTime();
    average( t );
    if( m_surgeCur > 0 && m_absCur > m_surgeCur ) m_surgeT = t;   // The held current lasted until now
    m_signedCur = i;
    m_absCur = ai;
    if( ai > m_peakCur ) m_peakCur = ai;
    setRegion( t, m_avgCur, true );
}

void Diode::paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
    Component::paint( p, option, widget );

    p->setBrush( Theme::line() );

 static const QPointF points[3] = {
        QPointF( 7, 0 ),
        QPointF(-8,-7 ),
        QPointF(-8, 7 )              };
    p->drawPolygon(points, 3);

    QPen pen = p->pen();
    pen.setWidth( 3 );
    p->setPen( pen );
    p->drawLine( 7, -6, 7, 6 );
   
    if( m_isZener ){
        p->drawLine( 7,-6, 4,-6 );
        p->drawLine( 7, 6, 10, 6 );
    }
    Component::paintSelected( p );
}

void Diode::simState( StateIO& s )
{
    eDiode::simState( s );
    s.io( m_isZener );
    s.io( m_region );
    s.io( m_signedCur );
    s.io( m_absCur );
    s.io( m_avgCur );
    s.io( m_peakCur );
    s.io( m_lastT );
    s.io( m_surgeT );
    s.io( m_surged );
}
