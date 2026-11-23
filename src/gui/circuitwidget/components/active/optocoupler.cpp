/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "optocoupler.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "e-diode.h"
#include "e-bjt.h"
#include "e-resistor.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"
#include "boolprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("Optocoupler",str)

// Models: docs/OPTOCOUPLER.md has where each number comes from.
// LED: IS fitted to the datasheet's typical VF with N = 1.8 [INFERRED].
QHash<QString, optoTran_t> OptoTransistor::m_models = {
    //            LED IS    N      CTR  gain  BVceo  Cje      Cjc
    { "4N35",   { { 7.47e-15, 1.8 }, 1.5, 500, 30,   100e-12, 20e-12 } },
    { "Custom", { { 7.47e-15, 1.8 }, 1.5, 500, 30,   100e-12, 20e-12 } },
};
QHash<QString, optoTriac_t> OptoTriac::m_models = {
    //               LED IS     N      IFT     IH      Ron  Roff   Vinh
    { "MOC3021",   { { 1.87e-13, 1.8 }, 8e-3,  100e-6, 11, 1e10,  0  } },
    { "MOC3041",   { { 6.56e-14, 1.8 }, 8e-3,  400e-6, 11, 1e10,  10 } },
    { "Custom",    { { 1.87e-13, 1.8 }, 8e-3,  100e-6, 11, 1e10,  0  } },
};
QHash<QString, optoLogic_t> OptoLogic::m_models = {
    //             LED IS     N      ITH     Ron  tPHL   tPLH   ICC     Ren
    { "6N137",   { { 8.72e-16, 1.8 }, 2.4e-3, 15, 50e-9, 48e-9, 3.3e-3, 6000 } },
    { "Custom",  { { 8.72e-16, 1.8 }, 2.4e-3, 15, 50e-9, 48e-9, 3.3e-3, 6000 } },
};

static QString modelList( QStringList l )
{
    l.sort();
    l.removeAll("Custom");
    l.append("Custom");
    return l.join(",");
}

static void powerOff(){ if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff(); }

// ---------------------------------------------------------------- OptoBase

OptoBase::OptoBase( QString type, QString id )
        : Component( type, id )
        , eElement( id )
{
    m_setting = false;
    m_led_d = { 1e-14, 1.8 };
    m_led = new eDiode( id+"-led" );
    setLabelPos(-16,-34, 0 );
}
OptoBase::~OptoBase(){}

void OptoBase::remove()
{
    delete m_led;
    Component::remove();
}

void OptoBase::stamp()
{
    applyLed();
    eNode* nA = m_pin[0]->getEnode();
    eNode* nK = m_pin[1]->getEnode();
    if( nA ) nA->addToNoLinList( this );
    if( nK ) nK->addToNoLinList( this );
}

void OptoBase::applyLed()
{
    m_led->setSatCur( m_led_d.satCur );
    m_led->setEmCoef( m_led_d.emCoef );
    m_led->setBrkDownV( 0 );
}

double OptoBase::ledCurrent()   // From the pin voltages (the LED's own linearization lags one iteration)
{
    if( !m_pin[0]->isConnected() || !m_pin[1]->isConnected() ) return 0;
    double v = m_pin[0]->getVoltage()-m_pin[1]->getVoltage();
    double x = v/( m_led_d.emCoef*0.025865 );
    if( x > 80 ) x = 80;
    return m_led_d.satCur*( qExp( x )-1 );
}

void OptoBase::custom()
{
    if( m_setting ) return;
    m_model = "Custom";
    powerOff();
}

void OptoBase::setLedIs( double v ){ if( v < 1e-25 ) v = 1e-25; if( v == m_led_d.satCur ) return; m_led_d.satCur = v; custom(); }
void OptoBase::setLedN ( double v ){ if( v < 0.5 ) v = 0.5;     if( v == m_led_d.emCoef ) return; m_led_d.emCoef = v; custom(); }

void OptoBase::paintLed( QPainter* p )
{
    p->setBrush( Theme::body() );
    p->drawRect( m_area );

    QPen pen = p->pen();
    pen.setWidthF( 1 );
    p->setPen( pen );
    p->drawLine( QPointF(-16,-8 ), QPointF(-10,-8 ) );   // Anode lead down to the LED
    p->drawLine( QPointF(-10,-8 ), QPointF(-10,-5 ) );
    p->drawLine( QPointF(-10, 3 ), QPointF(-10, 8 ) );
    p->drawLine( QPointF(-10, 8 ), QPointF(-16, 8 ) );
    p->setBrush( Theme::line() );
    QPointF tri[3] = { QPointF(-14,-5 ), QPointF(-6,-5 ), QPointF(-10, 2 ) };
    p->drawPolygon( tri, 3 );
    p->drawLine( QPointF(-14, 2.5 ), QPointF(-6, 2.5 ) );
    for( double dy : { -3.0, 2.0 } ){                       // Light arrows to the right
        p->drawLine( QPointF(-5, dy-1 ), QPointF(-1, dy+1 ) );
        p->drawLine( QPointF(-1, dy+1 ), QPointF(-3.5, dy+1 ) );
        p->drawLine( QPointF(-1, dy+1 ), QPointF(-1.8, dy-1.2 ) );
    }
    p->setBrush( Qt::NoBrush );
}

void OptoBase::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    paintLed( p );
}

void OptoBase::simState( StateIO& s )
{
    eElement::simState( s );
}

// ---------------------------------------------------------------- OptoTransistor

#undef  tr
#define tr(str) simulideTr("OptoTransistor",str)

Component* OptoTransistor::construct( QString type, QString id )
{ return new OptoTransistor( type, id ); }

LibraryItem* OptoTransistor::libraryItem()
{
    return new LibraryItem(
        tr("Optocoupler"),
        "Other Active",
        "optocoupler.svg",
        "Optocoupler",
        OptoTransistor::construct );
}

OptoTransistor::OptoTransistor( QString type, QString id )
              : OptoBase( type, id )
{
    m_area = QRectF(-16,-16, 32, 32 );
    m_baseNode = nullptr;
    m_stamped = 0;

    m_pin.resize( 5 );
    m_pin[0] = new Pin( 180, QPoint(-24,-8 ), id+"-anode"    , 0, this );
    m_pin[1] = new Pin( 180, QPoint(-24, 8 ), id+"-cathode"  , 1, this );
    m_pin[2] = new Pin( 0,   QPoint( 24,-8 ), id+"-collector", 2, this );
    m_pin[3] = new Pin( 0,   QPoint( 24, 8 ), id+"-emitter"  , 3, this );
    m_pin[4] = new Pin( 270, QPoint( 0, 24 ), id+"-base"     , 4, this );

    setNumEpins( P_COUNT );
    m_led->setEpin( 0, m_pin[0] );
    m_led->setEpin( 1, m_pin[1] );
    m_bjt = new eBJT( id+"-bjt" );
    m_bjt->setEpin( 0, m_pin[2] );
    m_bjt->setEpin( 1, m_pin[3] );
    m_bjt->setEpin( 2, m_ePin[P_B] );

    addPropGroup( { tr("Main"), {
        new StrProp<OptoTransistor>("Model", tr("Model"), modelList( m_models.keys() )
                                   , this, &OptoTransistor::model, &OptoTransistor::setModel, 0, "enum" ),
        new BoolProp<OptoTransistor>("Base_Pin", tr("Base Pin"), ""
                                    , this, &OptoTransistor::basePin, &OptoTransistor::setBasePin, propNoCopy ),
    },0} );
    addPropGroup( { tr("Parameters"), {
        new DoubProp<OptoTransistor>("CTR", tr("Current Transfer Ratio"), ""
                                    , this, &OptoTransistor::ctr, &OptoTransistor::setCtr ),
        new DoubProp<OptoTransistor>("Gain", tr("Transistor Gain"), ""
                                    , this, &OptoTransistor::gain, &OptoTransistor::setGain ),
        new DoubProp<OptoTransistor>("BVceo", tr("C-E Breakdown Voltage (0 = none)"), "V"
                                    , this, &OptoTransistor::bvceo, &OptoTransistor::setBvceo ),
        new DoubProp<OptoTransistor>("Cje", tr("B-E Capacitance at 0 V"), "pF"
                                    , this, &OptoTransistor::cje, &OptoTransistor::setCje ),
        new DoubProp<OptoTransistor>("Cjc", tr("B-C Capacitance at 0 V"), "pF"
                                    , this, &OptoTransistor::cjc, &OptoTransistor::setCjc ),
        new DoubProp<OptoTransistor>("LedIS", tr("LED Saturation Current"), "A"
                                    , this, &OptoTransistor::ledIs, &OptoTransistor::setLedIs ),
        new DoubProp<OptoTransistor>("LedN", tr("LED Emission Coefficient"), ""
                                    , this, &OptoTransistor::ledN, &OptoTransistor::setLedN ),
    },0} );

    setBasePin( false );
    m_setting = true;
    setModel("4N35");
    m_setting = false;
}
OptoTransistor::~OptoTransistor(){}

void OptoTransistor::remove()
{
    delete m_bjt;
    OptoBase::remove();
}

void OptoTransistor::setModel( QString m )
{
    if( !m_models.contains( m ) ) return;
    m_model = m;
    if( m == "Custom" ) return;
    powerOff();
    m_data  = m_models.value( m );
    m_led_d = m_data.led;
}

void OptoTransistor::setCtr  ( double v ){ if( v < 0 ) v = 0;   if( v == m_data.ctr   ) return; m_data.ctr   = v; custom(); }
void OptoTransistor::setGain ( double v ){ if( v < 1 ) v = 1;   if( v == m_data.gain  ) return; m_data.gain  = v; custom(); }
void OptoTransistor::setBvceo( double v ){ if( v < 0 ) v = 0;   if( v == m_data.bvceo ) return; m_data.bvceo = v; custom(); }
void OptoTransistor::setCje  ( double v ){ if( v < 0 ) v = 0;   if( v == m_data.cje   ) return; m_data.cje   = v; custom(); }
void OptoTransistor::setCjc  ( double v ){ if( v < 0 ) v = 0;   if( v == m_data.cjc   ) return; m_data.cjc   = v; custom(); }

void OptoTransistor::setBasePin( bool b )
{
    powerOff();
    m_basePin = b;
    m_pin[4]->setEnabled( b );
    m_pin[4]->setVisible( b );
    if( !b ) m_pin[4]->removeConnector();
    update();
}

void OptoTransistor::initialize()
{
    m_baseNode = nullptr;
    if( !m_basePin || !m_pin[4]->getEnode() ) m_baseNode = new eNode( m_elmId+"-base");
    m_stamped = 0;
}

void OptoTransistor::applyData()
{
    m_bjt->setGain( m_data.gain );
    m_bjt->setBvceo( m_data.bvceo );
    m_bjt->setCje( m_data.cje );
    m_bjt->setCjc( m_data.cjc );
}

void OptoTransistor::stamp()
{
    OptoBase::stamp();
    applyData();
    eNode* nB = m_pin[4]->getEnode();
    if( !m_basePin || !nB ) nB = m_baseNode;
    m_ePin[P_B     ]->setEnode( nB );
    m_ePin[P_PH_IN ]->setEnode( nB );                   // Photocurrent collector -> base
    m_ePin[P_PH_OUT]->setEnode( m_pin[2]->getEnode() );
    m_ePin[P_PH_IN ]->createCurrent();
    m_ePin[P_PH_OUT]->createCurrent();
    m_stamped = 0;
}

void OptoTransistor::voltChanged()   // LED current -> base photocurrent IF*CTR/gain
{
    double i = ledCurrent();
    if( i < 0 ) i = 0;
    i *= m_data.ctr/m_data.gain;
    if( qFabs( i-m_stamped ) <= 1e-12+1e-5*qFabs( i ) ) return;
    m_stamped = i;
    m_ePin[P_PH_IN ]->stampCurrent( i );
    m_ePin[P_PH_OUT]->stampCurrent(-i );
    Simulator::self()->notCorverged();
}

void OptoTransistor::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    paintLed( p );

    p->drawLine( QPointF( 5,-7 ), QPointF( 5, 7 ) );    // Transistor: base bar,
    p->drawLine( QPointF( 5,-3 ), QPointF( 12,-8 ) );   // collector to its pin,
    p->drawLine( QPointF( 5, 3 ), QPointF( 12, 8 ) );   // emitter
    p->drawLine( QPointF( 12,-8 ), QPointF( 16,-8 ) );
    p->drawLine( QPointF( 12, 8 ), QPointF( 16, 8 ) );
    if( m_basePin ){ p->drawLine( QPointF( 5, 0 ), QPointF( 0, 0 ) ); p->drawLine( QPointF( 0, 0 ), QPointF( 0, 16 ) ); }
    p->setBrush( Theme::line() );
    QPointF tri[3] = { QPointF( 12, 8 ), QPointF( 8.2, 7.4 ), QPointF( 10, 4.8 ) };
    p->drawPolygon( tri, 3 );

    Component::paintSelected( p );
}

void OptoTransistor::simState( StateIO& s )
{
    OptoBase::simState( s );
    s.io( m_stamped );
    s.io( m_basePin );
}

// ---------------------------------------------------------------- OptoTriac

#undef  tr
#define tr(str) simulideTr("OptoTriac",str)

Component* OptoTriac::construct( QString type, QString id )
{ return new OptoTriac( type, id ); }

LibraryItem* OptoTriac::libraryItem()
{
    return new LibraryItem(
        tr("Opto-Triac"),
        "Other Active",
        "optotriac.svg",
        "OptoTriac",
        OptoTriac::construct );
}

OptoTriac::OptoTriac( QString type, QString id )
         : OptoBase( type, id )
{
    m_area = QRectF(-16,-16, 32, 32 );
    m_midNode = nullptr;
    m_state = false;

    m_pin.resize( 4 );
    m_pin[0] = new Pin( 180, QPoint(-24,-8 ), id+"-anode"  , 0, this );
    m_pin[1] = new Pin( 180, QPoint(-24, 8 ), id+"-cathode", 1, this );
    m_pin[2] = new Pin( 0,   QPoint( 24,-8 ), id+"-mt2"    , 2, this );
    m_pin[3] = new Pin( 0,   QPoint( 24, 8 ), id+"-mt1"    , 3, this );

    // MT2 --d1--> mid --sw-- MT1, mid --d2--> MT2 (as the Triac)
    setNumEpins( P_COUNT );
    m_led->setEpin( 0, m_pin[0] );
    m_led->setEpin( 1, m_pin[1] );
    m_d1 = new eDiode( id+"-d1" );
    m_d1->setEpin( 0, m_ePin[P_D1A] );
    m_d1->setEpin( 1, m_ePin[P_D1K] );
    m_d1->setModel("Diode Default");
    m_d2 = new eDiode( id+"-d2" );
    m_d2->setEpin( 0, m_ePin[P_D2A] );
    m_d2->setEpin( 1, m_ePin[P_D2K] );
    m_d2->setModel("Diode Default");
    m_sw = new eResistor( id+"-sw" );
    m_sw->setEpin( 0, m_ePin[P_RM] );
    m_sw->setEpin( 1, m_pin[3] );

    addPropGroup( { tr("Main"), {
        new StrProp<OptoTriac>("Model", tr("Model"), modelList( m_models.keys() )
                              , this, &OptoTriac::model, &OptoTriac::setModel, 0, "enum" ),
        showStateProp()
    },0} );
    addPropGroup( { tr("Parameters"), {
        new DoubProp<OptoTriac>("IFT", tr("LED Trigger Current"), "mA"
                               , this, &OptoTriac::ift, &OptoTriac::setIft ),
        new DoubProp<OptoTriac>("IH", tr("Holding Current"), "µA"
                               , this, &OptoTriac::ih, &OptoTriac::setIh ),
        new DoubProp<OptoTriac>("Ron", tr("On Resistance"), "Ω"
                               , this, &OptoTriac::ron, &OptoTriac::setRon ),
        new DoubProp<OptoTriac>("Vinh", tr("Zero-Cross Inhibit Voltage (0 = random phase)"), "V"
                               , this, &OptoTriac::vinh, &OptoTriac::setVinh ),
        new DoubProp<OptoTriac>("LedIS", tr("LED Saturation Current"), "A"
                               , this, &OptoTriac::ledIs, &OptoTriac::setLedIs ),
        new DoubProp<OptoTriac>("LedN", tr("LED Emission Coefficient"), ""
                               , this, &OptoTriac::ledN, &OptoTriac::setLedN ),
    },0} );
    enableStates( true );

    m_setting = true;
    setModel("MOC3021");
    m_setting = false;
}
OptoTriac::~OptoTriac(){}

void OptoTriac::remove()
{
    delete m_d1;
    delete m_d2;
    delete m_sw;
    OptoBase::remove();
}

void OptoTriac::setModel( QString m )
{
    if( !m_models.contains( m ) ) return;
    m_model = m;
    if( m == "Custom" ) return;
    powerOff();
    m_data  = m_models.value( m );
    m_led_d = m_data.led;
}

void OptoTriac::setIft ( double v ){ if( v < 1e-6 ) v = 1e-6; if( v == m_data.ift  ) return; m_data.ift  = v; custom(); }
void OptoTriac::setIh  ( double v ){ if( v < 1e-9 ) v = 1e-9; if( v == m_data.ih   ) return; m_data.ih   = v; custom(); }
void OptoTriac::setRon ( double v ){ if( v < 1e-3 ) v = 1e-3; if( v == m_data.ron  ) return; m_data.ron  = v; custom(); }
void OptoTriac::setVinh( double v ){ if( v < 0 ) v = 0;       if( v == m_data.vinh ) return; m_data.vinh = v; custom(); }

void OptoTriac::initialize()
{
    clearStates();
    m_midNode = new eNode( m_elmId+"-mid");
    m_state = false;
}

void OptoTriac::stamp()
{
    OptoBase::stamp();
    eNode* n2 = m_pin[2]->getEnode();
    eNode* n1 = m_pin[3]->getEnode();
    m_ePin[P_D1A]->setEnode( n2 );
    m_ePin[P_D1K]->setEnode( m_midNode );
    m_ePin[P_D2A]->setEnode( m_midNode );
    m_ePin[P_D2K]->setEnode( n2 );
    m_ePin[P_RM ]->setEnode( m_midNode );
    if( n2 ) n2->addToNoLinList( this );
    if( n1 ) n1->addToNoLinList( this );
    m_state = false;
    m_sw->setRes( m_data.roff );
}

double OptoTriac::mainCurrent() { return m_sw->current(); }   // MT2 -> MT1

void OptoTriac::voltChanged()
{
    Simulator::self()->addToSettled( this );
}

// Fire and hold from each step's converged values: during the iterations the LED's voltage can
// be far off (an LED driven at 2 mA read 15 mA once and latched the triac on)
void OptoTriac::stepSettled()
{
    bool state = m_state;
    if( qFabs( m_sw->current() ) < m_data.ih ) state = false;
    if( ledCurrent() > m_data.ift ){
        if( m_state ) state = true;   // Fired and the LED still on: stays on, as a gate held on
        else{
            double v = m_pin[2]->getVoltage()-m_pin[3]->getVoltage();
            if( m_data.vinh <= 0 || qFabs( v ) < m_data.vinh ) state = true;
        }
    }
    if( state != m_state ){            // Switched in an event 1 ps later: a new solve
        m_state = state;
        Simulator::self()->cancelEvents( this );
        Simulator::self()->addEvent( 1, this );
    }
    if( m_state && qFabs( m_sw->current() ) > 0.1*m_data.ih ) reportState("ON", stWait );
    else                                                      reportState("OFF", stOff );
}

void OptoTriac::runEvent()
{
    m_sw->setRes( m_state ? m_data.ron : m_data.roff );
}

void OptoTriac::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    paintLed( p );

    p->drawLine( QPointF( 3,-7 ), QPointF( 16,-7 ) );   // Bars on MT2 (top) and MT1 (bottom)
    p->drawLine( QPointF( 3, 7 ), QPointF( 16, 7 ) );
    p->drawLine( QPointF( 16,-8 ), QPointF( 16,-7 ) );
    p->drawLine( QPointF( 16, 8 ), QPointF( 16, 7 ) );
    p->setBrush( Theme::line() );
    QPointF t1[3] = { QPointF( 3, 7 ), QPointF( 9, 7 ), QPointF( 6,-7 ) };    // Down
    QPointF t2[3] = { QPointF( 9,-7 ), QPointF( 15,-7 ), QPointF( 12, 7 ) };  // Up
    p->drawPolygon( t1, 3 );
    p->drawPolygon( t2, 3 );
    if( m_data.vinh > 0 ){                                  // Zero-cross mark
        QFont font = p->font();
        font.setPixelSize( 5 );
        p->setFont( font );
        p->drawText( QRectF( 2, 8.5, 14, 6 ), Qt::AlignCenter, "ZC");
    }
    Component::paintSelected( p );
}

void OptoTriac::simState( StateIO& s )
{
    OptoBase::simState( s );
    s.io( m_state );
}

// ---------------------------------------------------------------- OptoLogic

#undef  tr
#define tr(str) simulideTr("OptoLogic",str)

Component* OptoLogic::construct( QString type, QString id )
{ return new OptoLogic( type, id ); }

LibraryItem* OptoLogic::libraryItem()
{
    return new LibraryItem(
        tr("Opto Logic"),
        "Other Active",
        "optologic.svg",
        "OptoLogic",
        OptoLogic::construct );
}

OptoLogic::OptoLogic( QString type, QString id )
         : OptoBase( type, id )
{
    m_area = QRectF(-16,-20, 32, 40 );
    m_low = m_target = false;

    m_pin.resize( 6 );
    m_pin[0] = new Pin( 180, QPoint(-24,-8 ), id+"-anode"  , 0, this );
    m_pin[1] = new Pin( 180, QPoint(-24, 8 ), id+"-cathode", 1, this );
    m_pin[2] = new Pin( 0,   QPoint( 24,-16), id+"-vcc"    , 2, this );
    m_pin[3] = new Pin( 0,   QPoint( 24,-8 ), id+"-enable" , 3, this );
    m_pin[4] = new Pin( 0,   QPoint( 24, 8 ), id+"-out"    , 4, this );
    m_pin[5] = new Pin( 0,   QPoint( 24, 16), id+"-gnd"    , 5, this );
    const char* labels[4] = { "VCC", "VE", "VO", "GND" };
    for( int i=0; i<4; ++i ){
        m_pin[2+i]->setLabelText( labels[i] );
        m_pin[2+i]->setFontSize( 4 );
        m_pin[2+i]->setSpace( 1.2 );
    }
    setNumEpins( P_COUNT );
    m_led->setEpin( 0, m_pin[0] );
    m_led->setEpin( 1, m_pin[1] );
    m_rcc = new eResistor( id+"-rcc" );          // Supply current VCC -> GND
    m_rcc->setEpin( 0, m_ePin[P_CCV] );
    m_rcc->setEpin( 1, m_ePin[P_CCG] );
    m_rout = new eResistor( id+"-rout" );        // Output transistor VO -> GND
    m_rout->setEpin( 0, m_pin[4] );
    m_rout->setEpin( 1, m_ePin[P_PU] );
    m_ren = new eResistor( id+"-ren" );          // Enable pull-up VCC -> VE
    m_ren->setEpin( 0, m_pin[2] );
    m_ren->setEpin( 1, m_pin[3] );

    addPropGroup( { tr("Main"), {
        new StrProp<OptoLogic>("Model", tr("Model"), modelList( m_models.keys() )
                              , this, &OptoLogic::model, &OptoLogic::setModel, 0, "enum" ),
    },0} );
    addPropGroup( { tr("Parameters"), {
        new DoubProp<OptoLogic>("ITH", tr("Input Threshold Current"), "mA"
                               , this, &OptoLogic::ith, &OptoLogic::setIth ),
        new DoubProp<OptoLogic>("tPHL", tr("Delay to Low"), "ns"
                               , this, &OptoLogic::tphl, &OptoLogic::setTphl ),
        new DoubProp<OptoLogic>("tPLH", tr("Delay to High"), "ns"
                               , this, &OptoLogic::tplh, &OptoLogic::setTplh ),
        new DoubProp<OptoLogic>("LedIS", tr("LED Saturation Current"), "A"
                               , this, &OptoLogic::ledIs, &OptoLogic::setLedIs ),
        new DoubProp<OptoLogic>("LedN", tr("LED Emission Coefficient"), ""
                               , this, &OptoLogic::ledN, &OptoLogic::setLedN ),
    },0} );

    m_setting = true;
    setModel("6N137");
    m_setting = false;
}
OptoLogic::~OptoLogic(){}

void OptoLogic::remove()
{
    delete m_rcc;
    delete m_rout;
    delete m_ren;
    OptoBase::remove();
}

void OptoLogic::setModel( QString m )
{
    if( !m_models.contains( m ) ) return;
    m_model = m;
    if( m == "Custom" ) return;
    powerOff();
    m_data  = m_models.value( m );
    m_led_d = m_data.led;
}

void OptoLogic::setIth ( double v ){ if( v < 1e-6 ) v = 1e-6; if( v == m_data.ith  ) return; m_data.ith  = v; custom(); }
void OptoLogic::setTphl( double v ){ if( v < 0 ) v = 0;       if( v == m_data.tphl ) return; m_data.tphl = v; custom(); }
void OptoLogic::setTplh( double v ){ if( v < 0 ) v = 0;       if( v == m_data.tplh ) return; m_data.tplh = v; custom(); }

void OptoLogic::initialize()
{
    m_low = m_target = false;
}

void OptoLogic::stamp()
{
    OptoBase::stamp();
    eNode* nG = m_pin[5]->getEnode();
    m_ePin[P_PU ]->setEnode( nG );
    m_ePin[P_CCV]->setEnode( m_pin[2]->getEnode() );
    m_ePin[P_CCG]->setEnode( nG );
    for( int i : { 2, 3, 5 } ) if( m_pin[i]->getEnode() ) m_pin[i]->getEnode()->addToNoLinList( this );
    m_rcc->setRes( 5/m_data.icc );
    m_ren->setRes( m_data.renPu );
    m_rout->setRes( 1e9 );
    m_low = m_target = false;
}

bool OptoLogic::wanted()   // Output low: LED above threshold, enabled, supplied
{
    double vg = m_pin[5]->getVoltage();
    bool supplied = m_pin[2]->getVoltage()-vg > 2.5;
    bool enabled  = m_pin[3]->getVoltage()-vg > 1.4;   // VEL 0.8 V max, VEH 2 V min
    return supplied && enabled && ledCurrent() > m_data.ith;
}

void OptoLogic::voltChanged()
{
    Simulator::self()->addToSettled( this );
}

void OptoLogic::stepSettled()   // The output follows after the propagation delay
{
    bool w = wanted();
    if( w == m_target ) return;
    m_target = w;
    Simulator::self()->cancelEvents( this );
    if( w == m_low ) return;   // Back before the pending change happened
    double t = w ? m_data.tphl : m_data.tplh;
    Simulator::self()->addEvent( qMax( (uint64_t)1, (uint64_t)( t*1e12 ) ), this );
}

void OptoLogic::runEvent()
{
    if( m_target == m_low ) return;
    m_low = m_target;
    m_rout->setRes( m_low ? m_data.ron : 1e9 );
}

void OptoLogic::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    paintLed( p );

    p->drawLine( QPointF( 2,-4 ), QPointF( 2, 4 ) );     // Photodiode bar and the gate
    QPointF g[3] = { QPointF( 4,-4 ), QPointF( 4, 4 ), QPointF( 10, 0 ) };
    p->drawPolygon( g, 3 );
    p->drawEllipse( QPointF( 11.2, 0 ), 1.2, 1.2 );

    Component::paintSelected( p );
}

void OptoLogic::simState( StateIO& s )
{
    OptoBase::simState( s );
    s.io( m_low );
    s.io( m_target );
}
