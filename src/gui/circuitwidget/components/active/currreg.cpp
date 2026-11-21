/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "currreg.h"
#include "theme.h"
#include "simulator.h"
#include "circuit.h"
#include "itemlibrary.h"
#include "pin.h"

#include "doubleprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("CurrReg",str)

Component* CurrReg::construct( QString type, QString id )
{ return new CurrReg( type, id ); }

LibraryItem* CurrReg::libraryItem()
{
    return new LibraryItem(
        tr("Current Source IC"),
        "Other Active",
        "currreg.svg",
        "CurrReg",
        CurrReg::construct );
}

CurrReg::CurrReg( QString type, QString id )
       : Component( type, id )
       , eCurrReg( id )
{
    m_area = QRect(-16,-16, 32, 32 );
    setLabelPos(-16,-30, 0 );
    setValLabelPos( 20, 6, 0 );

    m_pin.resize( 3 );
    m_ePin[0] = m_pin[0] = new Pin(  90, QPoint( 0,-24 ), id+"-vp", 0, this );
    m_ePin[1] = m_pin[1] = new Pin( 270, QPoint( 0, 24 ), id+"-vm", 1, this );
    m_ePin[2] = m_pin[2] = new Pin(   0, QPoint( 24, 0 ), id+"-r",  2, this );
    m_pin[0]->setLabelText("V+");
    m_pin[1]->setLabelText("V–"); // U+2013
    m_pin[2]->setLabelText("R");
    for( Pin* p : m_pin ){ p->setFontSize( 5 ); p->setLabelColor( Theme::line() ); }

    Simulator::self()->addToUpdateList( this );

    setModel("LM334");

    addPropGroup( { tr("Main"), {
        new StrProp<CurrReg>("Model", tr("Model"), eCurrReg::modelNames().join(",")
                            , this, &CurrReg::modelStr, &CurrReg::setModelStr, 0,"enum" ),

        new DoubProp<CurrReg>("Temp", tr("Junction Temperature"),"°C"
                             , this, &CurrReg::temp, &CurrReg::setTemp ),
        showStateProp()
    },0} );

    addPropGroup( { tr("Electric"), {
        new DoubProp<CurrReg>("Vsense", tr("Sense Voltage (R to V-, at 25 °C)"),"mV"
                             , this, &CurrReg::vsense, &CurrReg::setVsense ),

        new DoubProp<CurrReg>("Ratio", tr("I_SET / I_BIAS (typical)"),""
                             , this, &CurrReg::ratio, &CurrReg::setRatio ),
    },0} );
    enableStates( true );

    setShowProp("Model");
}
CurrReg::~CurrReg(){}

void CurrReg::initialize()
{
    eCurrReg::initialize();
    m_region = regNone;
    m_fault.clear();
    clearStates();
}

void CurrReg::updateStep()
{
    if( Circuit::self()->animate() ) update();
    if( m_changed ) voltChanged(); // A property changed: solve again
}

void CurrReg::voltChanged()
{
    eCurrReg::voltChanged();
    if( m_stateLog ) Simulator::self()->addToSettled( this );
}

// Operating state once a step has converged (docs/PART_STATES.md):
//   OFF       no current (no headroom, reversed, or R open)
//   REG       regulating: V_R within 1 % of the sense voltage
//   LOW V     below the compliance voltage: V_R short of the sense voltage
//   FAULT     an absolute maximum rating exceeded (voltage, reverse voltage, current, power, R pin)
void CurrReg::stepSettled()
{
    const currRegData_t& d = modelData();
    double v  = headroom();
    double is = iSet();

    QString fault;
    if     ( v > d.vMax )         fault = "OVER V";
    else if( v < -d.vRev )        fault = "REVERSE V";
    else if( is > d.iMax )        fault = "OVER I";
    else if( v*is > d.pMax )      fault = "OVER P";
    else if( qFabs( vR() ) > d.vrMax ) fault = "R PIN V";

    region_t r;
    if( !fault.isEmpty() ) r = regFault;
    else if( is < 1e-9 || v <= 0 || !m_ePin[2]->getEnode() ) r = regOff;
    else{
        double vs  = vsenseNow();
        double err = ( vs-vR() )/vs;
        double lim = ( m_region == regOn ) ? 0.02 : 0.01;   // Hysteresis
        r = ( err < lim ) ? regOn : regLow;
    }
    if( r == m_region && fault == m_fault ) return;
    m_region = r;
    m_fault  = fault;
    if     ( r == regFault ) reportState( fault,   stFault );
    else if( r == regOff   ) reportState("OFF",    stOff );
    else if( r == regOn    ) reportState("REG",    stOn );
    else                     reportState("LOW V",  stWait );
}

void CurrReg::setModelStr( QString model )
{
    eCurrReg::setModel( model );
    update();
}

void CurrReg::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    bool on = Circuit::self()->animate() && m_region == regOn;
    p->setBrush( on ? QColor( Qt::yellow ) : Theme::body() );
    p->drawRect( m_area );

    p->setBrush( Qt::NoBrush );                        // Current source: two linked circles
    p->drawEllipse( QPointF( 0,-3.5 ), 4.5, 4.5 );
    p->drawEllipse( QPointF( 0, 3.5 ), 4.5, 4.5 );
    p->drawLine( QPointF( 16, 0 ), QPointF( 7, 0 ) );   // R senses the source
    p->setBrush( Theme::line() );
    QPointF pts[3] = { QPointF( 4.5, 0 ), QPointF( 8.5,-2 ), QPointF( 8.5, 2 ) };
    p->drawPolygon( pts, 3 );

    Component::paintSelected( p );
}

void CurrReg::simState( StateIO& s )
{
    eCurrReg::simState( s );
    s.io( m_region );
}
