/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "jfet.h"
#include "theme.h"
#include "simulator.h"
#include "circuit.h"
#include "itemlibrary.h"
#include "pin.h"

#include "doubleprop.h"
#include "boolprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("Jfet",str)

Component* Jfet::construct( QString type, QString id )
{ return new Jfet( type, id ); }

LibraryItem* Jfet::libraryItem()
{
    return new LibraryItem(
        tr("JFET"),
        "Transistors",
        "jfet.svg",
        "Jfet",
        Jfet::construct);
}

Jfet::Jfet( QString type, QString id )
    : Component( type, id )
    , eJfet( id )
{
    m_area = QRectF(-12, -14, 28, 28 );
    setLabelPos( 18, 0, 0 );

    m_pin.resize(3);
    m_ePin[0] = m_pin[0] = new Pin(  90, QPoint( 8,-16), id+"-Dren", 0, this );
    m_ePin[1] = m_pin[1] = new Pin( 270, QPoint( 8, 16), id+"-Sour", 1, this );
    m_ePin[2] = m_pin[2] = new Pin( 180, QPoint(-16, 8), id+"-Gate", 2, this );

    Simulator::self()->addToUpdateList( this );

    setModel("2N3819");

    addPropGroup( { tr("Main"), {
        new StrProp<Jfet>("Model", tr("Model"), eJfet::modelNames().join(",")
                         , this, &Jfet::modelStr, &Jfet::setModelStr, 0,"enum" ),

        new BoolProp<Jfet>("P_Channel", tr("P Channel"),""
                          , this, &Jfet::pChannel, &Jfet::setPchannel ),
        showStateProp()
    },0} );

    addPropGroup( { tr("Electric"), {
        new DoubProp<Jfet>("IDSS", tr("IDSS (at VGS = 0)"),"mA"
                          , this, &Jfet::idss, &Jfet::setIdss ),

        new DoubProp<Jfet>("Vp", tr("Pinch-off Voltage (VGS)"),"V"
                          , this, &Jfet::vp, &Jfet::setVp ),

        new DoubProp<Jfet>("Lambda", tr("Lambda (output slope)"),"1/V"
                          , this, &Jfet::lambda, &Jfet::setLambda ),
    },0} );

    addPropGroup( { tr("Gate"), {
        new DoubProp<Jfet>("SatCurrent", tr("Gate Saturation Current"),"pA"
                          , this, &Jfet::satCur, &Jfet::setSatCur ),

        new DoubProp<Jfet>("EmCoef", tr("Gate Emission Coefficient"),""
                          , this, &Jfet::emCoef, &Jfet::setEmCoef ),
    },0} );
    enableStates( true );
}
Jfet::~Jfet(){}

void Jfet::initialize()
{
    eJfet::initialize();
    m_region = regNone;
    clearStates();
}

void Jfet::updateStep()
{
    if( Circuit::self()->animate() ) update();
    if( m_changed ) voltChanged(); // A property changed: solve again (m_changed cleared at eJfet::voltChanged)
}

void Jfet::voltChanged()
{
    eJfet::voltChanged();
    if( m_stateLog ) Simulator::self()->addToSettled( this );   // Region from this step's final values
}

// Operating region once a step has converged (docs/PART_STATES.md), as the MOSFET's. P channel:
// polarities reversed. Vov = VGS - Vp is the gate drive above pinch-off:
//   OFF     VGS beyond pinch-off: no drain current
//   ACTIVE  amplifying (the FET's "saturation" region): VDS > Vov, ID set by VGS
//   OHMIC   a resistor set by VGS: VDS < Vov
// Each boundary has a hysteresis (0.05 V) so the state does not flicker on it.
void Jfet::stepSettled()
{
    double vgs = m_ePin[2]->isConnected() ? m_ePin[2]->getVoltage()-m_ePin[1]->getVoltage() : 0;
    double vds = m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage();
    if( m_pChannel ){ vgs = -vgs; vds = -vds; }
    vds = qAbs( vds );
    double vov = vgs-m_vto;

    region_t r = m_region;
    if( r == regNone ) r = ( vov <= 0.025 ) ? regOff : ( vds < vov ) ? regOhmic : regActive;
    else if( r == regOff ){ if( vov > 0.05 ) r = ( vds < vov ) ? regOhmic : regActive; }
    else if( vov < 0 ) r = regOff;
    else if( r == regActive ){ if( vds < vov-0.05 ) r = regOhmic; }
    else if( vds > vov+0.05 ) r = regActive;

    if( r == m_region ) return;
    m_region = r;
    if     ( r == regOff    ) reportState("OFF",    stOff );
    else if( r == regActive ) reportState("ACTIVE", stOn );
    else                      reportState("OHMIC",  stWait );
}

void Jfet::setModelStr( QString model )
{
    eJfet::setModel( model );
    update();
}

void Jfet::setPchannel( bool p )
{
    if( p != m_pChannel ) m_model = "Custom";
    m_pChannel = p;
    m_changed = true;
    update();
}

void Jfet::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    bool on = Circuit::self()->animate() && ( m_region == regActive || m_region == regOhmic );
    p->setBrush( on ? QColor( Qt::yellow ) : Theme::body() );
    p->drawEllipse( m_area );

    p->drawLine( 0,-8, 8,-8 );   // Drain
    p->drawLine( 8,-8, 8,-12 );
    p->drawLine( 0, 8, 8, 8 );   // Source
    p->drawLine( 8, 8, 8, 12 );
    p->drawLine(-12, 8, 0, 8 );  // Gate

    p->setBrush( Theme::line() );
    if( m_pChannel ){            // Arrow out of the channel
        QPointF pts[3] = { QPointF(-6, 8 ), QPointF(-2, 6 ), QPointF(-2, 10 ) };
        p->drawPolygon( pts, 3 );
    }else{                       // Arrow into the channel
        QPointF pts[3] = { QPointF(-1, 8 ), QPointF(-5, 6 ), QPointF(-5, 10 ) };
        p->drawPolygon( pts, 3 );
    }
    QPen pen( Theme::line(), 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
    p->setPen( pen );
    p->drawLine( 0,-11, 0, 11 ); // Channel

    Component::paintSelected( p );
}
