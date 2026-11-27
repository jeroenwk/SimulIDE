/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include <QtMath>
#include <QDebug>
#include <cstdlib>

#include "scr.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuit.h"
#include "e-diode.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"

#define tr(str) simulideTr("SCR",str)

Component* SCR::construct( QString type, QString id )
{ return new SCR( type, id ); }

LibraryItem* SCR::libraryItem()
{
    return new LibraryItem(
        tr("SCR"),
        "Rectifiers",
        "scr.png",
        "SCR",
        SCR::construct );
}

SCR::SCR( QString type, QString id )
     : Component( type, id )
     , eElement( id )
{
    m_area = QRectF(-10,-8, 20, 16 );
    setLabelPos(-12,-30, 0 );

    m_holdCurr = 0.0082;
    m_trigCurr = 0.01;
    m_gateRes  = 100;

    m_pin.resize( 3 );
    m_pin[0] = new Pin( 180, QPoint(-16, 0 ), id+"-lPin", 0, this);
    m_pin[1] = new Pin( 0,   QPoint( 16, 0 ), id+"-rPin", 1, this);
    m_pin[2] = new Pin( 0,   QPoint( 16, 8 ), id+"-gPin", 2, this);
    m_pin[2]->setPinAngle(-40 );
    m_pin[2]->setLength( 12 );

    setNumEpins( 6 );

    // Pin0--diode1--ePin0--midEnode--ePin1--resistor--Pin1
    // Pin2--resistGa--ePin2--gateEnode--ePin3--gateDiode--ePin4--|
    // The gate was a resistor to the cathode: a negative gate drive pulled the cathode and its
    // load below ground. A real gate-cathode is a PN junction (create-circuit #496).
    m_diode = new eDiode( id+"-dio1" );
    m_diode->setEpin( 0, m_pin[0] );
    m_diode->setEpin( 1, m_ePin[0] );
    m_diode->setModel( "Diode Default" );

    m_resistor = new eResistor( m_elmId+"-resistorA");
    m_resistor->setEpin( 0, m_ePin[1] );
    m_resistor->setEpin( 1, m_pin[1] );

    m_resistGa = new eResistor( m_elmId+"-resistorC");
    m_resistGa->setEpin( 0, m_pin[2] );
    m_resistGa->setEpin( 1, m_ePin[2] );

    m_gateDiode = new eDiode( id+"-gateDio" );
    m_gateDiode->setEpin( 0, m_ePin[3] );
    m_gateDiode->setEpin( 1, m_ePin[4] );
    m_gateDiode->setModel( "Diode Default" );

    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new DoubProp<SCR>("GateRes", tr("Gate Resistance"), "Ω"
                         , this, &SCR::gateRes, &SCR::setGateRes ),

        new DoubProp<SCR>("TrigCurr", tr("Trigger Current"), "mA"
                         , this, &SCR::trigCurr, &SCR::setTrigCurr ),

        new DoubProp<SCR>("HoldCurr", tr("Holding Current"), "mA"
                         , this, &SCR::holdCurr, &SCR::setHoldCurr ),
        showStateProp()
    },0} );
    enableStates( true );
}
SCR::~SCR()
{
    delete m_diode;
    delete m_resistor;
    delete m_resistGa;
    delete m_gateDiode;
}

void SCR::initialize()
{
    clearStates();
    m_midEnode = new eNode( m_elmId+"-mideNode");
    m_gateEnode = new eNode( m_elmId+"-gateeNode");
}

void SCR::stamp()
{
    // Pin0--diode1--ePin0--midEnode--ePin1--resistor--Pin1
    // Pin2-------------------------resistGa--ePin2---|
    m_state = false;

    eNode* nodeA = m_pin[0]->getEnode();
    eNode* nodeC = m_pin[1]->getEnode();
    eNode* nodeG = m_pin[2]->getEnode();

    //m_diode->getEpin(0)->setEnode( nodeA );
    m_diode->getEpin(1)->setEnode( m_midEnode );

    m_resistor->getEpin(0)->setEnode( m_midEnode );
    m_resistGa->getEpin(1)->setEnode( m_gateEnode );
    m_gateDiode->getEpin(0)->setEnode( m_gateEnode );
    m_gateDiode->getEpin(1)->setEnode( nodeC );

    if( nodeA ) nodeA->addToNoLinList( this );
    if( nodeC ) nodeC->addToNoLinList( this );
    if( nodeG ) nodeG->addToNoLinList( this );

    m_resistor->setRes( 10e5 );
    m_resistGa->setRes( m_gateRes );
}

void SCR::updateStep()
{
    if( !m_changed ) return;
    m_changed = false;

    voltChanged();
}

// Latch condition from the present pin values
bool SCR::latched()
{
    double anodCurr =  m_resistor->current();
    double cathCurr = -m_resistGa->current() - anodCurr;
    double cmult = 1/m_trigCurr;
    double amult = 1/m_holdCurr - cmult;

    // Holding: only the anode current that leaves through the cathode keeps the latch
    // (regeneration runs through the cathode junction). Anode current that leaves through
    // the gate (cathode opened by a reset switch, the gate held low) kept an SCR ON with no
    // cathode current at all, so closing the switch re-latched it without a trigger
    // (create-circuit #370).
    double iHold = qMax( 0.0, qMin( anodCurr, -cathCurr ) );
    bool state = -cathCurr*cmult + iHold*amult > 1;
    static const bool dbg = getenv("SIMULIDE_DEBUG_SCR");
    if( dbg )
        qDebug() << "[SCR]" << m_elmId << "t" << Simulator::self()->circTime()
                 << "vAC" << m_pin[0]->getVoltage() - m_pin[1]->getVoltage()
                 << "vGC" << m_pin[2]->getVoltage() - m_pin[1]->getVoltage()
                 << "Ia" << anodCurr << "Ik" << cathCurr << "Ihold" << iHold << "state" << m_state << "->" << state;
    return state;
}

void SCR::voltChanged()
{
    Simulator::self()->addToSettled( this );   // Latch state from this step's final values
    double voltAC = m_pin[0]->getVoltage() - m_pin[1]->getVoltage();
    double voltGC = m_pin[2]->getVoltage() - m_pin[1]->getVoltage();
    if( qFabs( voltAC ) < .01 && qFabs( voltGC ) < .01 ) return;  // Converged
    Simulator::self()->notCorverged();

    // Turning on follows the iterations; turning off waits for the step's solution
    // (stepSettled): while another part iterates (a UJT firing into the gate) the solver passes
    // through values far from it (the gate at -49 V), and an SCR latched off by one of them
    // dropped out for ~55 us at every gate pulse (create-circuit #497)
    if( !m_state && latched() )
    {
        m_state = true;
        m_resistor->setRes( 0.0105 );
    }
}

void SCR::runEvent() {;}  // Turned off in stepSettled: this event solves the circuit with it

// Part state (docs/PART_STATES.md): ON = conducting (amber, like a switch that is on), else OFF.
// m_state alone means "triggered": with gate current it stays set while the part blocks (reverse
// half-cycle, series diode), so ON also needs a main current above 10 % of the holding current
void SCR::stepSettled()
{
    if( m_state && !latched() )   // The solution of this step lets it go: off from now on
    {
        m_state = false;
        m_resistor->setRes( 10e5 );
        Simulator::self()->addEvent( 0, this );
    }
    if( m_state && m_resistor->current() > 0.1*m_holdCurr ) reportState("ON",  stWait );
    else                                    reportState("OFF", stOff );
}

void SCR::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

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

    Component::paintSelected( p );
}

void SCR::simState( StateIO& s )
{
    eElement::simState( s );
    s.io( m_gateRes );
    s.io( m_trigCurr );
    s.io( m_holdCurr );
    s.io( m_state );
}
