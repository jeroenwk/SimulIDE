/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <math.h>
#include <QPainter>
#include <QGraphicsProxyWidget>

#include "photosensor.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "propdialog.h"
#include "e-diode.h"
#include "e-bjt.h"
#include "e-resistor.h"
#include "e-node.h"
#include "pin.h"
#include "label.h"

#include "doubleprop.h"
#include "boolprop.h"
#include "stringprop.h"

// A value read back from its saved text ("400 µA" = 400*1e-6) can be one ulp off the model's
// table value: compare with a relative tolerance so a reopened model doesn't turn Custom.
static inline bool same( double a, double b ) { return qAbs( a-b ) <= 1e-9*qMax( qAbs( a ), qAbs( b ) ); }

#define tr(str) simulideTr("PhotoSensor",str)

// Models (docs/PHOTO.md has where each number comes from). Light in lux of illuminant A.
QHash<QString, photodiodeData_t> Photodiode::m_models = {
    //               sens      IS       N    BV     Rsh    Cj0     Vj   M
    { "BPW34",     { 70e-9,  6.2e-11, 1.0, 60,  5e9,  70e-12, 0.7, 0.62 } },
    // Small silicon cell (about 60 x 40 mm) [INFERRED, typical]: 100 mA and 0.60 V in full sun
    // (100 klx), so Isc 1 mA and Voc 0.481 V at 1 klx; shunt 1 M, Cj0 10 nF, breakdown 10 V
    { "Solar cell",{ 1e-6,   0.1/( exp( 0.6/0.025865 )-1 ), 1.0, 10, 1e6, 10e-9, 0.7, 0.5 } },
    { "Custom",    { 70e-9,  6.2e-11, 1.0, 60,  5e9,  70e-12, 0.7, 0.62 } },
};
static const double s_vt = 0.025865;   // eDiode's thermal voltage
QHash<QString, phototranData_t> Phototransistor::m_models = {
    //               sens (base) gain  IS      BVceo  Cje      Cjc
    { "BPW85A",    { 4.2e-9,  500,  1e-13,  70,  60e-12,  6e-12 } },
    { "BPW85B",    { 7.3e-9,  500,  1e-13,  70,  60e-12,  6e-12 } },
    { "BPW85C",    { 14.6e-9, 500,  1e-13,  70,  60e-12,  6e-12 } },
    { "Custom",    { 7.3e-9,  500,  1e-13,  70,  60e-12,  6e-12 } },
};

static QString modelList( QStringList l )
{
    l.sort();
    l.removeAll("Custom");
    l.append("Custom");
    return l.join(",");
}

// ---------------------------------------------------------------- PhotoSensor

PhotoSensor::PhotoSensor( QString type, QString id )
           : Dialed( type, id )
           , eElement( id )
{
    m_value  = 100;
    m_minVal = 0;
    m_maxVal = 1000;
    m_step   = 0;
    m_setFromCode = false;
    m_sens = 0;
    m_stamped = 0;
    m_setting = false;
    m_needUpdate = true;
    m_lightP = nullptr;
    m_lightPin = false;
    m_luxPerV = 100;
    m_pinLux = 0;

    setValLabelPos( 15,-20, 0 );
    setLabelPos(-16,-36, 0 );
}
PhotoSensor::~PhotoSensor(){}

void PhotoSensor::initialize()
{
    m_needUpdate = true;
}

void PhotoSensor::stamp()   // Before the inner elements' own stamps (they come later in the list)
{
    setNodes();
    m_pinLux = 0;
    if( m_lightPin && m_lightP->getEnode() ){
        m_lightP->getEnode()->voltChangedCallback( this );
        m_pinLux = qMax( 0.0, m_lightP->getVoltage() )*m_luxPerV;
    }
    m_ePin[P_PH_IN ]->createCurrent();
    m_ePin[P_PH_OUT]->createCurrent();
    m_stamped = photoCurrent();
    m_ePin[P_PH_IN ]->stampCurrent( m_stamped );
    m_ePin[P_PH_OUT]->stampCurrent(-m_stamped );
}

void PhotoSensor::updateStep()
{
    if( !m_needUpdate ) return;
    m_needUpdate = false;

    if( Simulator::self()->isRunning() ){
        double i = photoCurrent();
        if( i != m_stamped ){
            m_stamped = i;
            m_ePin[P_PH_IN ]->stampCurrent( i );
            m_ePin[P_PH_OUT]->stampCurrent(-i );
    }   }
    if( m_propDialog ) m_propDialog->updtValues();
    if( m_lightPin ) setValLabelText( getPropStr("Lux_per_V") );   // The dial is not used
    else if( !m_propDialog ) setValLabelText( getPropStr( showProp() ) );
}

void PhotoSensor::voltChanged()   // The light pin's node moved
{
    if( !m_lightPin ) return;
    m_pinLux = qMax( 0.0, m_lightP->getVoltage() )*m_luxPerV;
    double i = photoCurrent();
    if( qFabs( i-m_stamped ) <= 1e-15+1e-9*qFabs( i ) ) return;
    m_stamped = i;
    m_ePin[P_PH_IN ]->stampCurrent( i );
    m_ePin[P_PH_OUT]->stampCurrent(-i );
    Simulator::self()->notCorverged();
}

Pin* PhotoSensor::createLightPin( QPoint pos, int index, QString id )
{
    m_lightP = new Pin( 90, pos, id+"-light", index, this );
    m_lightP->setEnabled( false );
    m_lightP->setVisible( false );
    return m_lightP;
}

void PhotoSensor::setLightPin( bool b )
{
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_lightPin = b;
    m_lightP->setEnabled( b );
    m_lightP->setVisible( b );
    if( !b ) m_lightP->removeConnector();
    m_proxy->setVisible( !b );   // Light from the pin: no dial
    m_needUpdate = true;
    if( !Simulator::self()->isRunning() ) updateStep();
    update();
}

void PhotoSensor::setVal( double v )    { m_value = v; updtValue(); }
void PhotoSensor::setMinVal( double v ) { if( v < 0 ) v = 0; if( v > m_maxVal ) v = m_maxVal; m_minVal = v; updtValue(); }
void PhotoSensor::setMaxVal( double v ) { if( v < 1e-9 ) v = 1e-9; if( v < m_minVal ) v = m_minVal; m_maxVal = v; updtValue(); }

void PhotoSensor::setSens( double s )
{
    if( s < 0 ) s = 0;
    if( same( s, m_sens ) ) return;
    m_sens = s;
    m_needUpdate = true;
    custom();
}

void PhotoSensor::custom()
{
    if( m_setting ) return;
    m_model = "Custom";
}

void PhotoSensor::setLinkedValue( double v, int i )   // 0: dial position (0-1000), 1: lux
{
    if( i == 0 ) Dialed::setLinkedValue( v, i );
    else         setVal( v );
}

void PhotoSensor::dialChanged( int val )
{
    if( m_setFromCode ){ m_needUpdate = true; return; }   // Keep the exact value set from code
    m_value = m_minVal+val*( m_maxVal-m_minVal )/1000;
    if( m_step > 0 ) m_value = round( m_value/m_step )*m_step;
    m_needUpdate = true;
    if( !Simulator::self()->isRunning() ) updateStep();
}

void PhotoSensor::updtValue()
{
    if     ( m_value > m_maxVal ) m_value = m_maxVal;
    else if( m_value < m_minVal ) m_value = m_minVal;

    if( m_maxVal > m_minVal ){
        m_setFromCode = true;
        m_dialW.setValue( ( m_value-m_minVal )*1000/( m_maxVal-m_minVal ) );
        m_setFromCode = false;
    }
    m_needUpdate = true;
    if( !Simulator::self()->isRunning() ) updateStep();
    else if( m_propDialog ) m_propDialog->updtValues();
}

void PhotoSensor::stateRestored()
{
    if( m_maxVal <= m_minVal ) return;
    m_setFromCode = true;
    m_dialW.setValue( qRound( ( m_value-m_minVal )*1000/( m_maxVal-m_minVal ) ) );
    m_setFromCode = false;
}

void PhotoSensor::updateProxy()
{
    m_proxy->setPos( QPoint(-m_dialW.width()/2, 12 ) );
}

void PhotoSensor::paintArrows( QPainter* p, double x, double y )   // Two incoming light arrows ending at (x,y)
{
    for( double dx : { 0.0, 5.0 } ){
        double ex = x+dx, ey = y;
        p->drawLine( QPointF( ex-5, ey-5 ), QPointF( ex, ey ) );
        p->drawLine( QPointF( ex, ey ), QPointF( ex-2.5, ey ) );
        p->drawLine( QPointF( ex, ey ), QPointF( ex, ey-2.5 ) );
    }
}

void PhotoSensor::simState( StateIO& s )
{
    eElement::simState( s );
    s.io( m_value );
    s.io( m_minVal );
    s.io( m_maxVal );
    s.io( m_step );
    s.io( m_setFromCode );
    s.io( m_sens );
    s.io( m_stamped );
    s.io( m_needUpdate );
    s.io( m_lightPin );
    s.io( m_luxPerV );
    s.io( m_pinLux );
}

// ---------------------------------------------------------------- Photodiode

#undef  tr
#define tr(str) simulideTr("Photodiode",str)

Component* Photodiode::construct( QString type, QString id )
{ return new Photodiode( type, id ); }

LibraryItem* Photodiode::libraryItem()
{
    return new LibraryItem(
        tr("Photodiode"),
        "Rectifiers",
        "photodiode.svg",
        "Photodiode",
        Photodiode::construct );
}

Photodiode::Photodiode( QString type, QString id )
          : PhotoSensor( type, id )
{
    m_areaComp = QRectF(-10,-12, 20, 20 );
    m_area = m_areaComp;

    m_pin.resize( 3 );
    m_pin[0] = new Pin( 180, QPoint(-16, 0 ), id+"-anode"  , 0, this );
    m_pin[1] = new Pin( 0,   QPoint( 16, 0 ), id+"-cathode", 1, this );
    m_pin[2] = createLightPin( QPoint(-8,-16 ), 2, id );

    setNumEpins( P_COUNT );

    m_diode = new eDiode( id+"-diode" );
    m_diode->setEpin( 0, m_pin[0] );
    m_diode->setEpin( 1, m_pin[1] );
    m_shunt = new eResistor( id+"-shunt" );
    m_shunt->setEpin( 0, m_ePin[P_SH_A] );
    m_shunt->setEpin( 1, m_ePin[P_SH_K] );

    addPropGroup( { tr("Main"), {
        new DoubProp<Photodiode>("Lux", tr("Light"), "lx"
                                , this, &Photodiode::getVal, &Photodiode::setVal ),
        new DoubProp<Photodiode>("Min_Lux", tr("Minimum Light"), "lx"
                                , this, &Photodiode::minVal, &Photodiode::setMinVal ),
        new DoubProp<Photodiode>("Max_Lux", tr("Maximum Light"), "lx"
                                , this, &Photodiode::maxVal, &Photodiode::setMaxVal ),
        new DoubProp<Photodiode>("Dial_Step", tr("Dial Step"), "lx"
                                , this, &Photodiode::getStep, &Photodiode::setStep ),
        new StrProp<Photodiode>("Model", tr("Model"), modelList( m_models.keys() )
                               , this, &Photodiode::model, &Photodiode::setModel, 0, "enum" ),
        new BoolProp<Photodiode>("Light_Pin", tr("Light Input Pin"), ""
                            , this, &Photodiode::lightPin, &Photodiode::setLightPin, propNoCopy ),
        new DoubProp<Photodiode>("Lux_per_V", tr("Light Pin Scale"), "lx/V"
                            , this, &Photodiode::luxPerV, &Photodiode::setLuxPerV ),
    },0} );
    addPropGroup( { tr("Parameters"), {
        new DoubProp<Photodiode>("Sensitivity", tr("Photocurrent per lux"), "nA"
                                , this, &Photodiode::sens, &Photodiode::setSens ),
        new DoubProp<Photodiode>("IS", tr("Saturation Current"), "A"
                                , this, &Photodiode::satCur, &Photodiode::setSatCur ),
        new DoubProp<Photodiode>("N", tr("Emission Coefficient"), ""
                                , this, &Photodiode::emCoef, &Photodiode::setEmCoef ),
        new DoubProp<Photodiode>("BrkDownV", tr("Breakdown Voltage"), "V"
                                , this, &Photodiode::brkDown, &Photodiode::setBrkDown ),
        new DoubProp<Photodiode>("Shunt", tr("Shunt (dark current)"), "GΩ"
                                , this, &Photodiode::rShunt, &Photodiode::setRShunt ),
        new DoubProp<Photodiode>("Isc", tr("Short-circuit Current at 1 klx"), "mA"
                                , this, &Photodiode::isc, &Photodiode::setIsc ),
        new DoubProp<Photodiode>("Voc", tr("Open-circuit Voltage at 1 klx"), "V"
                                , this, &Photodiode::voc, &Photodiode::setVoc ),
    },0} );
    addPropGroup( { tr("Capacitance"), {
        new DoubProp<Photodiode>("Cj0", tr("Junction Capacitance at 0 V"), "pF"
                                , this, &Photodiode::cj0, &Photodiode::setCj0 ),
        new DoubProp<Photodiode>("Vj", tr("Junction Potential"), "V"
                                , this, &Photodiode::vj, &Photodiode::setVj ),
        new DoubProp<Photodiode>("M", tr("Grading Coefficient"), ""
                                , this, &Photodiode::mj, &Photodiode::setMj ),
    },0} );
    addPropGroup( { tr("Dial"), Dialed::dialProps(), 0 } );

    setModel("BPW34");
    setShowProp("Lux");
    setVal( 100 );
}
Photodiode::~Photodiode(){}

void Photodiode::remove()
{
    delete m_diode;
    delete m_shunt;
    Component::remove();
}

void Photodiode::initialize()
{
    PhotoSensor::initialize();
    applyData();
}

void Photodiode::setNodes()
{
    eNode* nA = m_pin[0]->getEnode();
    eNode* nK = m_pin[1]->getEnode();
    m_ePin[P_PH_IN ]->setEnode( nA );   // Photocurrent cathode -> anode inside: out of the anode pin
    m_ePin[P_PH_OUT]->setEnode( nK );
    m_ePin[P_SH_A  ]->setEnode( nA );
    m_ePin[P_SH_K  ]->setEnode( nK );
}

void Photodiode::stamp()
{
    applyData();
    PhotoSensor::stamp();
}

void Photodiode::applyData()
{
    m_diode->setSatCur( m_data.satCur );
    m_diode->setEmCoef( m_data.emCoef );
    m_diode->setBrkDownV( m_data.brkDown );
    m_diode->setCj0( m_data.cj0 );
    m_diode->setVj( m_data.vj );
    m_diode->setMj( m_data.mj );
    m_shunt->setRes( m_data.rShunt );
}

void Photodiode::setModel( QString m )
{
    if( !m_models.contains( m ) ) return;
    m_model = m;
    if( m == "Custom" ) return;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_data = m_models.value( m );
    m_sens = m_data.sens;
    m_needUpdate = true;
}

double Photodiode::satCur() { return m_data.satCur; }
double Photodiode::emCoef() { return m_data.emCoef; }
double Photodiode::brkDown(){ return m_data.brkDown; }
double Photodiode::cj0()    { return m_data.cj0; }
double Photodiode::vj()     { return m_data.vj; }
double Photodiode::mj()     { return m_data.mj; }

#define PD_SET( fn, field, lo ) \
void Photodiode::fn( double v ){ if( v < lo ) v = lo; if( same( v, m_data.field ) ) return; m_data.field = v; custom(); \
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff(); }
PD_SET( setSatCur, satCur, 1e-20 )
PD_SET( setEmCoef, emCoef, 0.1 )
PD_SET( setBrkDown, brkDown, 0 )
PD_SET( setRShunt, rShunt, 1 )
PD_SET( setCj0, cj0, 0 )
PD_SET( setVj, vj, 0.1 )
PD_SET( setMj, mj, 0.01 )

double Photodiode::isc() { return m_sens*1000; }
double Photodiode::voc() { return m_data.emCoef*s_vt*log( isc()/m_data.satCur+1 ); }

void Photodiode::setIsc( double v )
{
    if( v < 1e-15 ) v = 1e-15;
    double vo = voc();
    setSens( v/1000 );
    setVoc( vo );
}

void Photodiode::setVoc( double v )
{
    if( v < 0.01 ) v = 0.01;
    setSatCur( isc()/( exp( v/( m_data.emCoef*s_vt ) )-1 ) );
}

double Photodiode::diodeCurrent()   // Into the anode pin, through the part
{
    if( !m_pin[0]->isConnected() || !m_pin[1]->isConnected() ) return 0;
    double v = m_pin[0]->getVoltage()-m_pin[1]->getVoltage();
    return m_diode->junctionCurrent() + v/m_data.rShunt - m_stamped;
}

void Photodiode::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    p->setBrush( Theme::line() );
 static const QPointF points[3] = {
        QPointF( 7, 0 ),
        QPointF(-8,-7 ),
        QPointF(-8, 7 )              };
    p->drawPolygon( points, 3 );

    QPen pen = p->pen();
    pen.setWidth( 3 );
    p->setPen( pen );
    p->drawLine( 7, -6, 7, 6 );

    pen.setWidthF( 1 );
    p->setPen( pen );
    paintArrows( p, -2, -7 );

    Component::paintSelected( p );
}

void Photodiode::simState( StateIO& s )
{
    PhotoSensor::simState( s );
}

// ---------------------------------------------------------------- Phototransistor

#undef  tr
#define tr(str) simulideTr("Phototransistor",str)

Component* Phototransistor::construct( QString type, QString id )
{ return new Phototransistor( type, id ); }

LibraryItem* Phototransistor::libraryItem()
{
    return new LibraryItem(
        tr("Phototransistor"),
        "Transistors",
        "phototransistor.svg",
        "Phototransistor",
        Phototransistor::construct );
}

Phototransistor::Phototransistor( QString type, QString id )
               : PhotoSensor( type, id )
{
    m_areaComp = QRectF(-12,-14, 28, 28 );
    m_area = m_areaComp;
    m_baseNode = nullptr;
    setLabelPos( 18,-8, 0 );
    setValLabelPos( 18, 4, 0 );

    m_pin.resize( 4 );
    m_pin[0] = new Pin( 90,  QPoint( 8,-16 ), id+"-collector", 0, this );
    m_pin[1] = new Pin( 270, QPoint( 8, 16 ), id+"-emitter"  , 1, this );
    m_pin[2] = new Pin( 180, QPoint(-16, 0 ), id+"-base"     , 2, this );
    m_pin[3] = createLightPin( QPoint(-8,-16 ), 3, id );

    setNumEpins( P_COUNT );

    m_bjt = new eBJT( id+"-bjt" );
    m_bjt->setEpin( 0, m_pin[0] );
    m_bjt->setEpin( 1, m_pin[1] );
    m_bjt->setEpin( 2, m_ePin[P_B] );

    addPropGroup( { tr("Main"), {
        new DoubProp<Phototransistor>("Lux", tr("Light"), "lx"
                                     , this, &Phototransistor::getVal, &Phototransistor::setVal ),
        new DoubProp<Phototransistor>("Min_Lux", tr("Minimum Light"), "lx"
                                     , this, &Phototransistor::minVal, &Phototransistor::setMinVal ),
        new DoubProp<Phototransistor>("Max_Lux", tr("Maximum Light"), "lx"
                                     , this, &Phototransistor::maxVal, &Phototransistor::setMaxVal ),
        new DoubProp<Phototransistor>("Dial_Step", tr("Dial Step"), "lx"
                                     , this, &Phototransistor::getStep, &Phototransistor::setStep ),
        new StrProp<Phototransistor>("Model", tr("Model"), modelList( m_models.keys() )
                                    , this, &Phototransistor::model, &Phototransistor::setModel, 0, "enum" ),
        new BoolProp<Phototransistor>("Light_Pin", tr("Light Input Pin"), ""
                            , this, &Phototransistor::lightPin, &Phototransistor::setLightPin, propNoCopy ),
        new DoubProp<Phototransistor>("Lux_per_V", tr("Light Pin Scale"), "lx/V"
                            , this, &Phototransistor::luxPerV, &Phototransistor::setLuxPerV ),
        new BoolProp<Phototransistor>("Base_Pin", tr("Base Pin"), ""
                                     , this, &Phototransistor::basePin, &Phototransistor::setBasePin, propNoCopy ),
    },0} );
    addPropGroup( { tr("Parameters"), {
        new DoubProp<Phototransistor>("Sensitivity", tr("Base photocurrent per lux"), "nA"
                                     , this, &Phototransistor::sens, &Phototransistor::setSens ),
        new DoubProp<Phototransistor>("Gain", tr("Gain"), ""
                                     , this, &Phototransistor::gain, &Phototransistor::setGain ),
        new DoubProp<Phototransistor>("IS", tr("Saturation Current"), "A"
                                     , this, &Phototransistor::satCur, &Phototransistor::setSatCur ),
        new DoubProp<Phototransistor>("BVceo", tr("C-E Breakdown Voltage (0 = none)"), "V"
                                     , this, &Phototransistor::bvceo, &Phototransistor::setBvceo ),
    },0} );
    addPropGroup( { tr("Capacitance"), {
        new DoubProp<Phototransistor>("Cje", tr("B-E Capacitance at 0 V"), "pF"
                                     , this, &Phototransistor::cje, &Phototransistor::setCje ),
        new DoubProp<Phototransistor>("Cjc", tr("B-C Capacitance at 0 V"), "pF"
                                     , this, &Phototransistor::cjc, &Phototransistor::setCjc ),
    },0} );
    addPropGroup( { tr("Dial"), Dialed::dialProps(), 0 } );

    updateProxy();
    setBasePin( false );
    setModel("BPW85B");
    setShowProp("Lux");
    setVal( 100 );
}
Phototransistor::~Phototransistor(){}

void Phototransistor::remove()
{
    delete m_bjt;
    Component::remove();
}

void Phototransistor::initialize()
{
    PhotoSensor::initialize();
    m_baseNode = nullptr;                       // Internal base when the base pin is off or open
    if( !m_basePin || !m_pin[2]->getEnode() ) m_baseNode = new eNode( m_elmId+"-base");
    applyData();
}

void Phototransistor::setNodes()
{
    eNode* nB = m_pin[2]->getEnode();
    if( !m_basePin || !nB ) nB = m_baseNode;
    m_ePin[P_B     ]->setEnode( nB );
    m_ePin[P_PH_IN ]->setEnode( nB );                   // Photocurrent collector -> base
    m_ePin[P_PH_OUT]->setEnode( m_pin[0]->getEnode() );
}

void Phototransistor::stamp()
{
    applyData();
    PhotoSensor::stamp();
}

void Phototransistor::applyData()
{
    m_bjt->setGain( m_data.gain );
    m_bjt->setSatCur( m_data.satCur );
    m_bjt->setBvceo( m_data.bvceo );
    m_bjt->setCje( m_data.cje );
    m_bjt->setCjc( m_data.cjc );
}

void Phototransistor::setModel( QString m )
{
    if( !m_models.contains( m ) ) return;
    m_model = m;
    if( m == "Custom" ) return;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_data = m_models.value( m );
    m_sens = m_data.sens;
    m_needUpdate = true;
}

void Phototransistor::setBasePin( bool b )
{
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_basePin = b;
    m_pin[2]->setEnabled( b );
    m_pin[2]->setVisible( b );
    if( !b ) m_pin[2]->removeConnector();
    update();
}

void Phototransistor::updateProxy()
{
    m_proxy->setPos( QPoint(-m_dialW.width()-2, 8 ) );
}

double Phototransistor::gain()  { return m_data.gain; }
double Phototransistor::satCur(){ return m_data.satCur; }
double Phototransistor::bvceo() { return m_data.bvceo; }
double Phototransistor::cje()   { return m_data.cje; }
double Phototransistor::cjc()   { return m_data.cjc; }

#define PT_SET( fn, field, lo ) \
void Phototransistor::fn( double v ){ if( v < lo ) v = lo; if( same( v, m_data.field ) ) return; m_data.field = v; custom(); \
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff(); }
PT_SET( setGain, gain, 1 )
PT_SET( setSatCur, satCur, 1e-20 )
PT_SET( setBvceo, bvceo, 0 )
PT_SET( setCje, cje, 0 )
PT_SET( setCjc, cjc, 0 )

double Phototransistor::collectorCurrent()
{
    if( !m_pin[0]->isConnected() ) return 0;
    double ic = m_pin[0]->hasTrueCurrent() ? m_pin[0]->trueCurrent() : 0;
    return ic + m_stamped;   // The BJT's own collector current plus the photocurrent leaving it
}

void Phototransistor::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    p->setBrush( Theme::body() );
    p->drawEllipse( m_areaComp );
    if( m_basePin ) p->drawLine(-12, 0,-4, 0 );
    p->drawLine( -4,-8,-4,  8 );
    p->drawLine( -4,-4, 8,-12 );
    p->drawLine( -4, 4, 8, 12 );

    p->setBrush( Theme::line() );
 static const QPointF points[3] = {
        QPointF( 6  , 10.7 ),
        QPointF( 2.4, 10 ),
        QPointF( 4  , 7.5 ) };
    p->drawPolygon( points, 3 );

    p->setBrush( Qt::NoBrush );
    paintArrows( p, -8, -6 );

    Component::paintSelected( p );
}

void Phototransistor::simState( StateIO& s )
{
    PhotoSensor::simState( s );
    s.io( m_basePin );
}
