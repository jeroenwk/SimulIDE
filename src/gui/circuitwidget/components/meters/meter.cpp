/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>
#include <QFont>
#include <QPainter>

#include "theme.h"
#include "meter.h"
#include "appfonts.h"
#include "simulator.h"
#include "iopin.h"
#include "label.h"
#include "utils.h"

#include "boolprop.h"
#include "doubleprop.h"
#include "stringprop.h"

#define tr(str) simulideTr("Meter",str)

Meter::Meter( QString type, QString id )
     : Component( type, id )
     , eResistor( id )
     , m_display( this )
{
    m_area = QRectF( -24, -24, 50, 32 );
    m_graphical = true;
    m_switchPins = false;

    m_pin.resize( 3 );
    m_ePin[0] = m_pin[0] = new Pin( 270, QPoint(-8, 16), id+"-lPin", 0, this);
    m_pin[0]->setColor( Qt::red );

    m_ePin[1] = m_pin[1] = new Pin( 270, QPoint(8, 16), id+"-rPin", 1, this);
    m_pin[0]->setWireColor( QColor( 220, 40, 40 ) );     // Test leads: red, and COM grey
    m_pin[1]->setWireColor( QColor( 128, 128, 128 ) );

    m_pin[2] = m_outPin = new IoPin( 0, QPoint(32,-8), id+"-outnod", 0, this, source );
    m_outPin->setOutHighV( 0 );
    m_outPin->setOutState( true );

    m_idLabel->setPos(-12,-24);
    setLabelPos(-24,-40, 0);

    QFont f( AppFonts::sans(), 10, QFont::Bold );
    f.setPixelSize(12);
    m_display.setFont(f);
    m_display.setBrush( Theme::screenText() );
    m_display.setPos( -22, -22 );
    m_display.setVisible( true );

    m_reading    = rInstant;
    m_readingStr = "Instant";
    m_avgTime    = 0.1;
    m_block      = 100000000000ULL;   // 100 ms
    m_dispValue  = 0;
    initialize();

    addPropGroup( { tr("Main"), {
        new StrProp <Meter>("Reading", tr("Reading"), "Instant,Mean,RMS;"+tr("Instant")+","+tr("Mean")+","+tr("RMS")
                           , this, &Meter::reading, &Meter::setReading, 0, "enum" ),
        new DoubProp<Meter>("Avg_Time", tr("Mean/RMS over"), "ms"
                           , this, &Meter::avgTime, &Meter::setAvgTime ),
    }, 0 } );

    Simulator::self()->addToUpdateList( this );
}
Meter::~Meter(){}

void Meter::setReading( QString r )
{
    QString l = r.trimmed().toLower();
    if     ( l == "mean" ) { m_reading = rMean; m_readingStr = "Mean"; }
    else if( l == "rms"  ) { m_reading = rRms;  m_readingStr = "RMS";  }
    else                   { m_reading = rInstant; m_readingStr = "Instant"; }

    Simulator* sim = Simulator::self();
    if( !sim->isRunning() || m_reading == rInstant ) return;   // Started from stamp()
    sim->pauseSim();                                           // Changed during a run: from now
    m_blockStart = m_lastT = sim->circTime();
    m_sum = m_sum2 = 0;
    m_haveResult = false;
    m_held = instant();
    m_ePin[0]->changeCallBack( this );
    m_ePin[1]->changeCallBack( this );
    sim->resumeSim();
}

void Meter::setAvgTime( double t )
{
    if( t <= 0 ) return;
    m_avgTime = t;
    m_block = (uint64_t)( t*1e12 + 0.5 );
    if( m_block == 0 ) m_block = 1;
}

void Meter::initialize()
{
    m_crashed = false;
    m_blockStart = m_lastT = 0;
    m_held = m_sum = m_sum2 = m_result = 0;
    m_haveResult = false;
    m_dispValue = 0;                  // A new run: the output pin starts at 0, not at the last
    m_outPin->setOutHighV( 0 );       // run's reading (the first run after opening differed);
                                      // high (at 0 V) as the constructor leaves it: in stamp()
}

void Meter::stamp()
{
    eResistor::stamp();
    m_outPin->setOutState( true );    // Not in initialize(): it stamps the node before the nodes are
                                      // initialized, and the changed-node list looped (npn-curve hung)
    if( m_reading == rInstant ) return;
    uint64_t now = Simulator::self()->circTime();     // (a run resumed after a pause: from here)
    m_blockStart = m_lastT = now;
    m_sum = m_sum2 = 0;
    m_held = instant();
    m_ePin[0]->changeCallBack( this );
    m_ePin[1]->changeCallBack( this );
}

void Meter::voltChanged() { Simulator::self()->addToSettled( this ); }

void Meter::stepSettled()                  // Once per converged step that changed a pin
{
    if( m_reading == rInstant ) return;
    integrateTo( Simulator::self()->circTime() );
    m_held = instant();
}

double Meter::blockValue( double sum, double sum2, double span )
{
    if( span <= 0 ) return m_reading == rRms ? qFabs( m_held ) : m_held;
    return m_reading == rRms ? qSqrt( qMax( 0.0, sum2/span ) ) : sum/span;
}

void Meter::integrateTo( uint64_t t )
{
    if( t < m_lastT ) return;
    uint64_t end = m_blockStart + m_block;
    if( t >= end )
    {
        double dt = (double)( end - m_lastT );       // Close the block running now
        m_result = blockValue( m_sum + m_held*dt, m_sum2 + m_held*m_held*dt, (double)m_block );
        m_haveResult = true;
        m_sum = m_sum2 = 0;
        uint64_t whole = ( t - end )/m_block;         // Blocks without a change: the held value
        if( whole ) m_result = blockValue( 0, 0, 0 );
        m_blockStart = end + whole*m_block;
        m_lastT = m_blockStart;
    }
    double dt = (double)( t - m_lastT );
    m_sum  += m_held*dt;
    m_sum2 += m_held*m_held*dt;
    m_lastT = t;
}

double Meter::evalAt( uint64_t t )
{
    uint64_t end = m_blockStart + m_block;
    if( t >= end + m_block ) return blockValue( 0, 0, 0 );     // A whole block of the held value
    if( t >= end ){                                            // The running block has ended
        double dt = (double)( end - m_lastT );
        return blockValue( m_sum + m_held*dt, m_sum2 + m_held*m_held*dt, (double)m_block );
    }
    if( m_haveResult ) return m_result;
    double dt = (double)( t - m_lastT );                       // Before the first block ends: so far
    return blockValue( m_sum + m_held*dt, m_sum2 + m_held*m_held*dt, (double)( t - m_blockStart ) );
}

double Meter::measured()
{
    if( m_reading == rInstant || ( !Simulator::self()->isRunning() && !Simulator::self()->isPaused() ) )
        return instant();
    return evalAt( Simulator::self()->circTime() );
}

double Meter::value()
{
    return m_reading == rInstant ? m_dispValue : measured();
}

bool Meter::setPropStr( QString prop, QString val )
{
    if( prop =="SwitchPins" )       // Old: TODELETE
    {
        if( val == "true" ) { m_Hflip = -1; setflip(); }
    }
    else return Component::setPropStr( prop, val );
    return true;
}

void Meter::updateStep()
{
    QString sign = " ";
    QString mult = " ";
    int decimals = 3;
    double value = qFabs(m_dispValue);

    if( value < 1e-9 ) value = 0;
    else{
        value *= 1e12;
        if( m_dispValue < 0 ) sign = "-";
        valToUnit( value, mult, decimals )
    }
    if( value > 999 )
    {
        m_display.setText( " ----" );
        m_crashed = true;
    }
    else m_display.setText( sign+QString::number( value,'f', decimals ).left(5)+"\n"+mult+m_unit
                          + ( m_reading == rRms ? " rms" : m_reading == rMean ? " avg" : "" ) );

    m_outPin->setOutHighV( m_dispValue );
    m_outPin->setOutState( true );
}

void Meter::setSwitchPins( bool s )
{
    if( s == m_switchPins ) return;
    m_switchPins = s;

    qreal x0 = s ? 8 :-8;
    qreal x1 = s ?-8 : 8;

    m_pin[0]->setX( x0 );
    m_pin[1]->setX( x1 );

    m_pin[0]->isMoved();
    m_pin[1]->isMoved();
}

void Meter::setflip()
{
    Component::setflip();

    int xlabelpos = -22*m_Hflip;
    int ylabelpos = -22*m_Vflip;
    if( m_Vflip < 0 ) ylabelpos -= 16;

    m_display.setPos( xlabelpos, ylabelpos );
    m_display.setTransform( QTransform::fromScale( m_Hflip, m_Vflip ) );
}

void Meter::updateLabelColors()
{
    Component::updateLabelColors();
    m_display.setBrush( Theme::screenText() );
}

void Meter::paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
    Component::paint( p, option, widget );
    p->setBrush( Theme::screenBg() );
    p->drawRoundedRect( QRectF(-24,-24, 48, 32 ), 1, 1 );

    /*if( m_hidden ) return;

    QPointF points[3] = {
    QPointF( 26,-11 ),
    QPointF( 31, -8 ),
    QPointF( 26, -5 )   };
    p->drawPolygon(points, 3);*/

    Component::paintSelected( p );
}

void Meter::simState( StateIO& s )
{
    eResistor::simState( s );
    s.io( m_reading );
    s.io( m_avgTime );
    s.io( m_block );
    s.io( m_blockStart );
    s.io( m_lastT );
    s.io( m_held );
    s.io( m_sum );
    s.io( m_sum2 );
    s.io( m_result );
    s.io( m_haveResult );
    s.io( m_dispValue );
    s.io( m_switchPins );
}
