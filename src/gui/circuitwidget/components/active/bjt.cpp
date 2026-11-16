/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <math.h>

#include "bjt.h"
#include "theme.h"
#include "itemlibrary.h"
#include "circuitwidget.h"
#include "connector.h"
#include "simulator.h"
#include "circuit.h"
#include "pin.h"

#include "doubleprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("BJT",str)

Component* BJT::construct( QString type, QString id )
{ return new BJT( type, id ); }

LibraryItem* BJT::libraryItem()
{
    return new LibraryItem(
        tr("BJT"),
        "Transistors",
        "bjt.png",
        "BJT",
        BJT::construct );
}

BJT::BJT( QString type, QString id )
   : Component( type, id )
   , eBJT( id )
{
    m_area = QRectF( -12, -14, 28, 28 );
    setLabelPos(18, 0, 0);

    m_pin.resize( 3 );
    m_ePin[0] = m_pin[0] = new Pin( 90,  QPoint( 8,-16), id+"-collector", 0, this );
    m_ePin[1] = m_pin[1] = new Pin( 270, QPoint( 8, 16), id+"-emiter"   , 1, this );
    m_ePin[2] = m_pin[2] = new Pin( 180, QPoint(-16, 0), id+"-base"     , 0, this );

    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new BoolProp<BJT>("PNP"  , tr("PNP"),""
                         , this, &BJT::pnp , &BJT::setPnp ),

        new DoubProp<BJT>("Gain" , tr("Gain"),""
                         , this, &BJT::gain, &BJT::setGain ),

        new DoubProp<BJT>("Vcrit", tr("Threshold"),"V"
                         , this, &BJT::threshold, &BJT::setThreshold ),

        new DoubProp<BJT>("IS", tr("Saturation Current"),"A"
                         , this, &BJT::satCur, &BJT::setSatCur ),

        new DoubProp<BJT>("VA", tr("Early Voltage (0 = none)"),"V"
                         , this, &BJT::early, &BJT::setEarly ),

        new DoubProp<BJT>("BVceo", tr("C-E Breakdown Voltage (0 = none)"),"V"
                         , this, &BJT::bvceo, &BJT::setBvceo ),

        new DoubProp<BJT>("BVebo", tr("E-B Breakdown Voltage (0 = none)"),"V"
                         , this, &BJT::bvebo, &BJT::setBvebo ),
        showStateProp()
    },0} );

    addPropGroup( { tr("Capacitance"), {
        new DoubProp<BJT>("Cje", tr("B-E Capacitance at 0 V"), "pF"
                          , this, &BJT::cje, &BJT::setCje ),
        new DoubProp<BJT>("Vje", tr("B-E Junction Potential"), "V"
                          , this, &BJT::vje, &BJT::setVje ),
        new DoubProp<BJT>("Mje", tr("B-E Grading Coefficient"), ""
                          , this, &BJT::mje, &BJT::setMje ),
        new DoubProp<BJT>("Cjc", tr("B-C Capacitance at 0 V"), "pF"
                          , this, &BJT::cjc, &BJT::setCjc ),
        new DoubProp<BJT>("Vjc", tr("B-C Junction Potential"), "V"
                          , this, &BJT::vjc, &BJT::setVjc ),
        new DoubProp<BJT>("Mjc", tr("B-C Grading Coefficient"), ""
                          , this, &BJT::mjc, &BJT::setMjc )
    },0} );
    enableStates( true );
}
BJT::~BJT(){}

void BJT::initialize()
{
    eBJT::initialize();
    m_region = regNone;
    clearStates();
}

void BJT::updateStep()
{
    if( Circuit::self()->animate() ) update();
    if( m_changed ) eBJT::voltChanged(); // m_changed cleared at eBJT::voltChanged
}

void BJT::voltChanged()
{
    eBJT::voltChanged();
    if( m_stateLog ) Simulator::self()->addToSettled( this );   // Region from this step's final values
}

// Operating region, taken once a time step has converged (docs/PART_STATES.md). PNP: polarities reversed.
//   OFF     cut-off: VBE below 0.45 V, collector current below ~10 uA at the default threshold
//   ACTIVE  amplifying: IC = gain x IB
//   SAT     saturated, fully on: VCE below 0.2 V (base-collector junction forward biased)
//   BREAKDOWN  C-E avalanche past BVceo, or E-B past BVebo (fault): above 10 uA, back below 1 uA
// Each boundary has a hysteresis (0.05 V on, 0.1 V off) so the state does not flicker on it.
void BJT::stepSettled()
{
    double pnp = m_PNP ? -1 : 1;
    double vB = m_ePin[2]->getVoltage();
    double vE = m_ePin[1]->getVoltage();
    double vC = m_ePin[0]->getVoltage();
    double vbe = pnp*( vB-vE );
    double vce = pnp*( vC-vE );

    region_t r = m_region;
    double ibd = qMax( qFabs( m_bdCurr ), qFabs( m_ebCurr ) ); // BVceo or BVebo: 10 uA on, 1 uA off
    if( ibd > 1e-5 || ( r == regBreak && ibd > 1e-6 ) ) r = regBreak;
    else if( r == regBreak ) r = regNone;
    if( r == regBreak ) ;
    else if( r == regNone ) r = ( vbe < 0.475 ) ? regOff : ( vce < 0.25 ) ? regSat : regActive;
    else if( r == regOff ){ if( vbe > 0.5 ) r = ( vce < 0.2 ) ? regSat : regActive; }
    else if( vbe < 0.45 ) r = regOff;
    else if( r == regActive ){ if( vce < 0.2 ) r = regSat; }
    else if( vce > 0.3 ) r = regActive;

    if( r == m_region ) return;
    m_region = r;
    if     ( r == regOff    ) reportState("OFF",    stOff );
    else if( r == regActive ) reportState("ACTIVE", stOn );
    else if( r == regSat    ) reportState("SAT",    stWait );
    else                      reportState("BREAKDOWN", stFault );
}

void BJT::setPnp( bool pnp )
{
    m_PNP = pnp;
    m_changed = true;
    update();
}

void BJT::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    
    if( Circuit::self()->animate()
     && fabs(m_baseCurr) > 1e-4 ) p->setBrush( Qt::yellow );
    else                          p->setBrush( Theme::body() );

    p->drawEllipse( m_area );
    p->drawLine(-12, 0,-4,  0 );
    p->drawLine( -4,-8,-4,  8 );
    p->drawLine( -4,-4, 8,-12 );
    p->drawLine( -4, 4, 8, 12 );
    
    p->setBrush( Theme::line() );
    if( m_PNP ){
        QPointF points[3] = {
        QPointF( 0.1, 6.8 ),
        QPointF( 2.4, 10 ),
        QPointF( 4, 7.5 ) };
        p->drawPolygon(points, 3);
    }else{
        QPointF points[3] = {
        QPointF( 6  , 10.7 ),
        QPointF( 2.4, 10 ),
        QPointF( 4  , 7.5 ) };
        p->drawPolygon(points, 3);
    }
    Component::paintSelected( p );
}
