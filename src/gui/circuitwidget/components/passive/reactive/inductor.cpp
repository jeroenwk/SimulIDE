/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <math.h>

#include "inductor.h"
#include "itemlibrary.h"
#include "chargeview.h"
#include "pin.h"

#include "doubleprop.h"

#define tr(str) simulideTr("Inductor",str)

Component* Inductor::construct( QString type, QString id )
{ return new Inductor( type, id ); }

LibraryItem* Inductor::libraryItem()
{
    return new LibraryItem(
        tr("Inductor"),
        "Reactive",
        "inductor.png",
        "Inductor",
        Inductor::construct);
}

Inductor::Inductor( QString type, QString id )
        : Reactive( type, id )
{
    m_area = QRectF(-10,-6, 20, 12 );

    m_pin[0]->setLength( 4 );
    m_pin[1]->setLength( 4 );

    m_value = m_inductance = 1; // H

    addPropGroup( { tr("Main"), {
        new DoubProp<Inductor>("Inductance", tr("Inductance"), "H"
                              , this, &Inductor::value, &Inductor::setValue ),

        new DoubProp<Inductor>("Resistance", tr("Resistance"), "µΩ"
                              , this, &Inductor::resist , &Inductor::setResist ),

        new DoubProp<Inductor>("InitVolt", tr("Initial Current"), "A"
                              , this, &Inductor::initCurr, &Inductor::setInitCurr ),

        new DoubProp<Inductor>("MaxCurrent", tr("Rated Current (0 = not set)"), "A"
                              , this, &Inductor::maxCurrent, &Inductor::setMaxCurrent ),

        new DoubProp<Inductor>("CurrentScale", tr("Current gauge 100 % at (0 = circuit)"), "A"
                              , this, &Inductor::currentScale, &Inductor::setCurrentScale ),

        new DoubProp<Inductor>("ReaStep", tr("Reactive Step"), "ns"
                              ,this, &Inductor::reaStep , &Inductor::setReaStep, 0 )
    },0 } );

    m_currentScale = 0;
    m_maxCurrent   = 0;

    setShowProp("Inductance");
    setPropStr("Inductance", "1");

    m_gauge = new ChargeGauge( this, this );
    m_gauge->setVisible( false );
}
Inductor::~Inductor(){}

void Inductor::setCurrentValue( double c )
{
    m_inductance = c;
    m_changed = true;
}

void Inductor::setCurrentScale( double a )
{
    m_currentScale = ( a > 0 ) ? a : 0;
    if( currentShown() ) m_gauge->refresh();
    update();
}

void Inductor::setMaxCurrent( double a )
{
    m_maxCurrent = ( a > 0 ) ? a : 0;
    update();
}

void Inductor::updateStep()
{
    Reactive::updateStep();                 // Repaints the part (coil colour)
    if( currentShown() ) m_gauge->refresh();
    else if( m_gauge->isVisible() ) m_gauge->setVisible( false );
}

bool Inductor::currentShown( bool active )
{
    if( active && !ChargeView::self()->active() ) return false;
    if( dynamic_cast<Component*>( parentItem() ) ) return false;   // Inside a part: the relay's coil
    return m_pin[0]->isConnected() && m_pin[1]->isConnected();
}

void Inductor::chargeDisplayChanged()
{
    if( currentShown() ) m_gauge->refresh();
    else                 m_gauge->setVisible( false );
    update();
}

double Inductor::currentRef()
{
    if( m_currentScale > 0 ) return m_currentScale;
    return ChargeView::self()->iref();
}

double Inductor::currentLevel()
{
    double ref = currentRef();
    if( ref <= 0 ) return 0;
    return qAbs( ChargeView::cleanAmps( indCurr() ) )/ref;
}

QStringList Inductor::currentFlags()
{
    QStringList flags;
    if( m_maxCurrent > 0 && qAbs( indCurr() ) > m_maxCurrent ) flags << "overcurrent";
    if( currentLevel() > 1 )                                    flags << "overflow";
    return flags;
}

bool Inductor::gaugeWarn()
{
    return m_maxCurrent > 0 && qAbs( indCurr() ) > m_maxCurrent;
}

double Inductor::gaugeTick()
{
    double ref = currentRef();
    if( m_maxCurrent > 0 && ref > 0 && m_maxCurrent < ref ) return m_maxCurrent/ref;
    return -1;
}

QString Inductor::gaugeText( int mode )
{
    double i = indCurr();
    QString pct = ChargeView::pctStr( currentLevel(), ChargeView::cleanAmps( i ) == 0 );
    if     ( mode == ChargeView::labelVolt    ) return ChargeView::ampStr( i );
    else if( mode == ChargeView::labelPercent ) return pct;
    return ChargeView::ampStr( i )+" · "+pct;
}

void Inductor::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    QPen pen = p->pen();
    pen.setWidth( 2 );
    if( currentShown() )                    // Coloured by current: + from pin 0 to pin 1
        pen.setColor( ChargeView::scaleColor( ChargeView::cleanAmps( indCurr() ), currentRef() ) );
    p->setPen( pen );
    
    p->drawArc( QRectF(-12,-4.5, 10, 10 ),-45*16 , 220*16 );
    p->drawArc( QRectF( -5,-4.5, 10, 10 ), 225*16,-270*16 );
    p->drawArc( QRectF(  2,-4.5, 10, 10 ), 225*16,-220*16 );

    Component::paintSelected( p );
}
