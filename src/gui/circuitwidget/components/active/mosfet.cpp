/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "mosfet.h"
#include "theme.h"
#include "simulator.h"
#include "circuit.h"
#include "itemlibrary.h"
#include "iopin.h"

#include "doubleprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("Mosfet",str)

Component* Mosfet::construct( QString type, QString id )
{ return new Mosfet( type, id ); }

LibraryItem* Mosfet::libraryItem()
{
    return new LibraryItem(
        tr("Mosfet"),
        "Transistors",
        "mosfet.png",
        "Mosfet",
        Mosfet::construct);
}

Mosfet::Mosfet( QString type, QString id )
      : Component( type, id )
      , eMosfet( id )
{
    m_area = QRectF(-12, -14, 28, 28 );
    setLabelPos( 18, 0, 0 );

    m_pin.resize(3);
    m_ePin[0] = m_pin[0] = new Pin(  90, QPoint(8,-16), id+"-Dren", 0, this );
    m_ePin[1] = m_pin[1] = new Pin( 270, QPoint(8, 16), id+"-Sour", 1, this );
    m_ePin[2] = m_pin[2] = new IoPin( 180, QPoint(-16, 0), id+"-Gate", 0, this, input );
    
    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new BoolProp<Mosfet>("P_Channel", tr("P Channel"),""
                                , this, &Mosfet::pChannel, &Mosfet::setPchannel ),

        new BoolProp<Mosfet>("Depletion", tr("Depletion"),""
                            , this, &Mosfet::depletion, &Mosfet::setDepletion ),
            },0} );

            addPropGroup( { tr("Electric"), {
        new DoubProp<Mosfet>("RDSon"    , tr("RDSon"),"Ω"
                            , this, &Mosfet::rdson, &Mosfet::setRDSon),

        new DoubProp<Mosfet>("Threshold", tr("Threshold"),"V"
                            , this, &Mosfet::threshold, &Mosfet::setThreshold )
    },0} );
}
Mosfet::~Mosfet(){}

void Mosfet::initialize()
{
    eMosfet::initialize();
    m_region = regOff;
    setStatus("OFF", stOff );
}

void Mosfet::updateStep()
{
    if( Circuit::self()->animate() ) update();
    updateRegion();

    if( !m_changed ) return;
    m_changed = false;

    updateValues();
    voltChanged();
}

// Operating region for the part state label ("Show Part States") and the Messages log, once
// per frame from the terminal voltages (P channel: polarities reversed). Vov = VGS - Vth is
// the gate drive above the model's turn-on voltage (negative for depletion / JFET-like parts):
//   OFF     VGS below the turn-on voltage (pinch-off for depletion): no drain current
//   ACTIVE  amplifying (the "saturation" region of a FET): VDS > Vov, ID set by VGS
//   OHMIC   fully on, a resistor (RDSon): VDS < Vov. Same amber as a saturated BJT: a switch that is on
// Each boundary has a hysteresis (0.05 V / 0.1 V) so the label does not flicker on it.
void Mosfet::updateRegion()
{
    if( !Simulator::self()->isRunning() ) return;
    double vgs = m_ePin[2]->isConnected() ? m_ePin[2]->getVoltage()-m_ePin[1]->getVoltage() : 0;
    double vds = m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage();
    if( m_Pchannel ){ vgs = -vgs; vds = -vds; }
    double vov = vgs-m_Gth;

    region_t r = m_region;
    if( r == regOff ){ if( vov > 0.05 ) r = ( vds < vov ) ? regOhmic : regActive; }
    else if( vov < 0 ) r = regOff;
    else if( r == regActive ){ if( vds < vov-0.05 ) r = regOhmic; }
    else if( vds > vov+0.05 ) r = regActive;

    if( r == m_region ) return;
    m_region = r;
    if     ( r == regOff    ) setStatus("OFF",    stOff );
    else if( r == regActive ) setStatus("ACTIVE", stOn );
    else                      setStatus("OHMIC",  stWait );
}

void Mosfet::setPchannel( bool p )
{
    m_Pchannel = p;
    m_changed = true;
    update();
}
void Mosfet::setDepletion( bool d )
{
    m_depletion = d;
    m_changed = true;
    update();
}

void Mosfet::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w);
    
    if( Circuit::self()->animate() && m_gateV > 0 )  p->setBrush( Qt::yellow );
    else                                             p->setBrush( Theme::body() );

    p->drawEllipse( m_area );
    
    p->drawLine(-12, 0,-4, 0 );
    p->drawLine( -4,-8,-4, 8 );
    
    p->drawLine( 0,-7, 8,-7 );
    p->drawLine( 0, 0, 8, 0 );
    p->drawLine( 0, 7, 8, 7 );
    
    p->drawLine( 8,-12, 8,-7 );
    p->drawLine( 8, 12, 8, 0 );
    
    p->setBrush( Theme::line() );
    if( m_Pchannel )
    {
         QPointF points[3] = {
         QPointF( 7, 0 ),
         QPointF( 3,-2 ),
         QPointF( 3, 2 ) };
         p->drawPolygon( points, 3 );
    } else {
        QPointF points[3] = {
        QPointF( 1, 0 ),
        QPointF( 5,-2 ),
        QPointF( 5, 2 )     };
        p->drawPolygon( points, 3 );
    }
    if( m_depletion )
    {
        QPen pen(Theme::line(), 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        p->setPen( pen );
        p->drawLine( 0,-9, 0, 9 );
    } else {
        p->drawLine( 0,-9, 0,-5 );
        p->drawLine( 0,-2, 0, 2 );
        p->drawLine( 0, 5, 0, 9 );
    }
    Component::paintSelected( p );
}
