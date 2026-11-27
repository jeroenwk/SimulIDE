/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QSet>
#include <QSignalBlocker>
#include <QGraphicsProxyWidget>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

#include "rotaryswitch.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "e-node.h"
#include "e-resistor.h"
#include "pin.h"
#include "switch_base.h"

#include "stringprop.h"
#include "boolprop.h"
#include "doubleprop.h"
#include "intprop.h"

#define tr(str) simulideTr("RotarySwitch",str)

static const int    MAX_CONTACTS = 12;       // Poles × positions, as on the SR25 / SR26 deck
static const char*  POLES = "ABCD";

// Unit vector at deg, clockwise from 12 o'clock
static QPointF clockwise( double deg )
{
    double a = qDegreesToRadians( deg );
    return QPointF( sin( a ), -cos( a ) );
}

// Where the i-th of n pins of a side stands from the centre of that side
static int rowOffset( int i, int n ) { return ( 2*i - (n-1) )*8; }

// Where a wire connects, on the 8 px grid: 8 px outside the body. The body is 40 px around its
// centre, or 48 when a side carries 4 pins: then the names of two neighbouring sides would
// meet in their corner
static int pinEnd( int nLeft, int nRight, int nTop, int nBottom )
{
    int most = qMax( qMax( nLeft, nRight ), qMax( nTop, nBottom ) );
    return ( most > 3 ) ? 56 : 48;
}

static QString throwName( int t, int positions )
{ return QString( POLES[t/positions] ) + QString::number( t%positions + 1 ); }

//--- The knob ----------------------------------------------------------------

RotaryKnob::RotaryKnob( QWidget* parent )
          : QDial( parent )
{
    setFixedSize( 40, 40 );
    setRange( 0, 3 );
    setSingleStep( 1 );
    setPageStep( 1 );
    setFocusPolicy( Qt::ClickFocus );
    setCursor( Qt::PointingHandCursor );
    setAttribute( Qt::WA_TranslucentBackground );

    m_start    = -45;
    m_dragging = false;
    m_offset   = 0;
}

double RotaryKnob::angleAt( QPointF pos )
{
    QPointF d = pos - rect().center();
    if( d.isNull() ) return pointerAngle();
    return qRadiansToDegrees( atan2( d.x(), -d.y() ) );
}

void RotaryKnob::paintEvent( QPaintEvent* )
{
    QPainter p( this );
    p.setRenderHint( QPainter::Antialiasing, true );
    QPointF c = rect().center();

    p.setPen( QPen( QColor( 110, 110, 110 ), 1 ) );       // One tick per detent
    for( int v = minimum(); v <= maximum(); ++v )
    {
        QPointF dir = clockwise( m_start + 30.0*v );
        p.drawLine( c + dir*16.5, c + dir*19.5 );
    }
    p.setPen( QPen( QColor( 130, 130, 135 ), 1.5 ) );     // The knob
    p.setBrush( QColor( 25, 25, 28 ) );
    p.drawEllipse( c, 14, 14 );

    QPointF dir = clockwise( pointerAngle() );            // Its pointer
    p.setPen( QPen( Qt::white, 2, Qt::SolidLine, Qt::RoundCap ) );
    p.drawLine( c + dir*5, c + dir*12 );
}

void RotaryKnob::mousePressEvent( QMouseEvent* e )
{
    if( e->button() != Qt::LeftButton ){ QDial::mousePressEvent( e ); return; }

    m_pressPos = e->pos();
    m_dragging = false;
    double delta = pointerAngle() - angleAt( e->pos() );
    m_offset = delta - 360*qFloor( ( delta + 180 )/360.0 );      // -180..180: the knob keeps its grip
    e->accept();
}

void RotaryKnob::mouseMoveEvent( QMouseEvent* e )
{
    if( !( e->buttons() & Qt::LeftButton ) ) return;
    if( !m_dragging )
    {
        if( ( e->pos() - m_pressPos ).manhattanLength() < 6 ) return;
        m_dragging = true;
    }
    double target = angleAt( e->pos() ) + m_offset;
    target -= 360*qFloor( ( target + 180 )/360.0 );
    setValue( qBound( minimum(), qRound( ( target - m_start )/30.0 ), maximum() ) );
    e->accept();
}

void RotaryKnob::mouseReleaseEvent( QMouseEvent* e )
{
    if( e->button() != Qt::LeftButton ){ QDial::mouseReleaseEvent( e ); return; }

    if( !m_dragging )                                              // A click: one detent
        triggerAction( ( e->modifiers() & Qt::ShiftModifier ) ? SliderSingleStepSub : SliderSingleStepAdd );
    m_dragging = false;
    e->accept();
}

void RotaryKnob::wheelEvent( QWheelEvent* e )
{
    triggerAction( e->angleDelta().y() > 0 ? SliderSingleStepSub : SliderSingleStepAdd );
    e->accept();
}

//--- The switch --------------------------------------------------------------

Component* RotarySwitch::construct( QString type, QString id )
{ return new RotarySwitch( type, id ); }

LibraryItem* RotarySwitch::libraryItem()
{
    return new LibraryItem(
        tr("Rotary Switch"),
        "Switches",
        "rotaryswitch.png",
        "RotarySwitch",
        RotarySwitch::construct);
}

RotarySwitch::RotarySwitch( QString type, QString id )
          : Component( type, id )
          , eElement(  id )
{
    m_graphical = true;
    m_radius    = 40;

    m_poles     = 1;
    m_positions = 4;
    m_position  = 0;
    m_shorting  = false;
    m_transit    = 0;
    m_overlap    = 1e-3;
    m_bounceTime = 0;
    m_bounces    = 5;
    m_contactRes = 0.05;
    m_onAdmit    = 20;
    m_at         = 0;
    m_mode       = modeNone;
    m_started    = false;
    m_endClosed  = true;
    m_isClosed   = false;
    m_bounceLeft = 0;
    m_bounceN    = 0;
    for( int p=0; p<4; ++p ) m_chatter[p] = -1;

    setLabelPos( -56, -64, 0 );
    setValLabelPos( 8, -64, 0 );

    m_knob = new RotaryKnob();
    m_proxy = Circuit::self()->addWidget( m_knob );
    m_proxy->setParentItem( this );
    m_proxy->setPos( -20, -20 );
    m_proxy->setTransformOriginPoint( 20, 20 );    // The knob's centre: it turns about it, and stays there

    Simulator::self()->addToUpdateList( this );

    setupPins();
    placeKnob();

    QObject::connect( m_knob, &QAbstractSlider::valueChanged, [=](int v){ dialChanged(v); } );
    m_keyEventConn = QObject::connect( Circuit::self(), &Circuit::keyEvent
                          , [=]( QString k, bool pressed ){ if( pressed && k == m_key ) step( 1 ); } );

    setShowProp( "Position" );

    addPropGroup( { tr("Main"), {
        new IntProp <RotarySwitch>("Poles",     tr("Poles"), ""
                            , this, &RotarySwitch::poles, &RotarySwitch::setPoles, propNoCopy, "uint" ),

        new IntProp <RotarySwitch>("Positions", tr("Positions"), ""
                            , this, &RotarySwitch::positions, &RotarySwitch::setPositions, propNoCopy, "uint" ),

        new IntProp <RotarySwitch>("Position",  tr("Position"), ""
                            , this, &RotarySwitch::position, &RotarySwitch::setPosition ),

        new BoolProp<RotarySwitch>("Shorting",  tr("Shorting (make before break)"), ""
                            , this, &RotarySwitch::shorting, &RotarySwitch::setShorting ),

        new StrProp <RotarySwitch>("Key",       tr("Key"), ""
                            , this, &RotarySwitch::key, &RotarySwitch::setKey ),
    }, 0} );

    addPropGroup( { tr("Contacts"), {
        new DoubProp<RotarySwitch>("Contact_Res", tr("Contact Resistance"), "mΩ"
                            , this, &RotarySwitch::contactRes, &RotarySwitch::setContactRes ),

        new DoubProp<RotarySwitch>("Transit_Time", tr("Transit Time (0 = none)"), "ms"
                            , this, &RotarySwitch::transitTime, &RotarySwitch::setTransitTime ),

        new DoubProp<RotarySwitch>("Overlap_Time", tr("Overlap Time (0 = none)"), "ms"
                            , this, &RotarySwitch::overlapTime, &RotarySwitch::setOverlapTime ),

        new DoubProp<RotarySwitch>("Bounce_Time", tr("Bounce Time (0 = none)"), "ms"
                            , this, &RotarySwitch::bounceTime, &RotarySwitch::setBounceTime ),

        new IntProp <RotarySwitch>("Bounces", tr("Bounces"), ""
                            , this, &RotarySwitch::bounces, &RotarySwitch::setBounces, 0, "uint" ),
    }, 0} );

    showPosition();
}

RotarySwitch::~RotarySwitch()
{
    for( eResistor* c : m_contact ) delete c;
    m_contact.clear();
}

//--- Properties --------------------------------------------------------------

void RotarySwitch::setPoles( int p )
{
    p = qBound( 1, p, 4 );
    if( p == m_poles ) return;

    m_poles = p;
    m_positions = qMin( m_positions, MAX_CONTACTS/m_poles );
    setupPins();
}

void RotarySwitch::setPositions( int n )
{
    n = qBound( 3, qMin( n, MAX_CONTACTS/m_poles ), 12 );
    if( n == m_positions ) return;

    m_positions = n;
    setupPins();
}

void RotarySwitch::setPosition( int pos )
{
    pos = qBound( 1, pos, m_positions ) - 1;
    if( pos == m_position ) return;

    m_position = pos;
    QSignalBlocker blocker( m_knob );
    m_knob->setValue( pos );
    m_changed = true;
    showPosition();
    if( !Simulator::self()->isRunning() ) updateStep();
}

void RotarySwitch::setContactRes( double r )
{
    m_contactRes = ( r > 1e-6 ) ? r : 1e-6;
    m_onAdmit = 1.0/m_contactRes;

    for( eResistor* c : m_contact )
        if( c->admit() > 0 ) c->setAdmit( m_onAdmit );
}

void RotarySwitch::setKey( QString key )
{
    if( key.size() > 1 ) key = key.left( 1 );

    if( !Circuit::self()->loading() && !Circuit::self()->restoring() )
    {
        key = key.toUpper();                       // Typed (dialog, CLI): a capital, or none
        if( !key.isEmpty() && !SwitchBase::validKey( key ) ) key = "";
    }
    m_key = key;
}

QString RotarySwitch::propHelp( QString prop )
{
    if( prop == "Poles" )     return tr("1 to 4. Each pole has its common (A, B, C, D, on the left) and one throw per position (A1, A2, ...). Poles × positions is at most 12, as on the SR25 / SR26 deck.");
    if( prop == "Positions" ) return tr("3 to 12, 30° per detent.");
    if( prop == "Position" )  return tr("Where the knob stands: 1 to Positions. Set it here, drag or click the knob (Shift + click: the other way), or turn the mouse wheel over it.");
    if( prop == "Shorting" )  return tr("Yes: make before break. The next throw connects first, the other one opens after Overlap Time. No: break before make. The pole touches nothing for Transit Time.");
    if( prop == "Key" )       return tr("Capital letter A–Z. Press Shift + this letter to turn the switch one detent clockwise while the simulation runs.");
    if( prop == "Overlap_Time" ) return tr("Shorting only: how long the two throws of a pole are both connected while it changes position.");
    if( prop == "Transit_Time" ) return tr("Not shorting only: how long the pole touches nothing while it changes position.");
    if( prop == "Bounce_Time" )  return tr("Each contact chatters for this long when it makes (not shorting) or breaks (shorting): Bounces changes that end in the new state. 0: a clean change.");
    if( prop == "Contact_Res" )  return tr("Resistance of a closed contact (50 mΩ max. on the Alpha SR26 datasheet).");
    return "";
}

//--- Pins and contacts -------------------------------------------------------

Pin* RotarySwitch::addPin( int angle, QPoint pos, QString name, int index )
{
    QSet<QGraphicsItem*> had;                      // The names we had: the pin adds one
    for( QGraphicsItem* child : childItems() ) if( dynamic_cast<QGraphicsSimpleTextItem*>( child ) ) had << child;

    Pin* pin = new Pin( angle, pos, m_id+"-"+name, index, this );
    pin->setLabelText( name );                     // The pin's own name: the CLI and the triggers use it
    pin->setLabelColor( QColor( 0, 0, 0 ) );       // In the theme's text colour, on the body's own colour

    QGraphicsSimpleTextItem* text = nullptr;      // Its text item, a child of ours
    for( QGraphicsItem* child : childItems() )
        if( !had.contains( child ) ) if( QGraphicsSimpleTextItem* t = dynamic_cast<QGraphicsSimpleTextItem*>( child ) ){ text = t; break; }

    m_pin.push_back( pin );
    if( !text ) return pin;

    // Where the name lies, horizontal, just inside the rim on the pin's row. The rim is 5 px
    // beyond the pin's row at the text's outer edge (reach); its centre is half a text further in
    int   d     = qAbs( ( angle % 180 == 0 ) ? pos.y() : pos.x() );
    qreal reach = sqrt( double( m_radius*m_radius ) - ( d+5 )*( d+5 ) ) - 2;
    QRectF  r   = text->boundingRect();
    QPointF centre;
    if( angle == 180 )      centre = QPointF( -reach + r.width()/2,  pos.y() );
    else if( angle == 0 )   centre = QPointF(  reach - r.width()/2,  pos.y() );
    else if( angle == 90 )  centre = QPointF( pos.x(), -reach + r.height()/2 );
    else                    centre = QPointF( pos.x(),  reach - r.height()/2 );
    m_names.push_back( { pin, text, centre } );
    return pin;
}

// The names read horizontal and unmirrored, however the part is turned or flipped. An item
// turns about its transformOriginPoint and mirrors about its corner (Qt), and the part turns
// by rotation() and mirrors by (m_Hflip, m_Vflip): each name turns back by that much, mirrors
// back, and is set so that its centre lands where it belongs. The pin puts its own placement
// back on a move, a turn or a flip (moveSignal): this runs after it
void RotarySwitch::placeLabels()
{
    for( NameLabel& n : m_names )
    {
        QGraphicsSimpleTextItem* text = n.name;
        QPointF c = text->boundingRect().center();

        text->setTransformOriginPoint( c );
        text->setTransform( QTransform::fromScale( m_Hflip, m_Vflip ) );
        text->setRotation( -rotation()*m_Hflip*m_Vflip );
        text->setPos( n.centre - QPointF( c.x()*m_Hflip, c.y()*m_Vflip ) );
    }
}

void RotarySwitch::setupPins()
{
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();

    for( eResistor* c : m_contact ) delete c;      // The contacts first: they hold the ePins
    for( uint i=0; i<m_ePin.size(); i += 2 ) delete m_ePin[i];
    for( Pin* pin : m_pin ) deletePin( pin );
    m_contact.clear();
    m_ePin.clear();
    m_pin.clear();
    m_names.clear();

    m_position = qBound( 0, m_position, m_positions-1 );

    int P = m_poles, N = m_positions, T = P*N;

    int nRight  = ( T+2 )/3;                       // The throws: the right, the top, the bottom
    int nTop    = ( T-nRight+1 )/2;
    int nBottom = T - nRight - nTop;

    int end = pinEnd( P, nRight, nTop, nBottom );
    m_radius = end - 8;
    m_area = QRectF( -m_radius, -m_radius, 2*m_radius, 2*m_radius );

    for( int p=0; p<P; ++p )                       // The commons, on the left
        addPin( 180, QPoint( -end, rowOffset( p, P ) ), QString( POLES[p] ), p );

    int t = 0;
    for( int i=0; i<nRight;  ++i, ++t ) addPin( 0,   QPoint(  end, rowOffset( i, nRight ) ),   throwName( t, N ), P+t );
    for( int i=0; i<nTop;    ++i, ++t ) addPin( 90,  QPoint( rowOffset( i, nTop ),    -end ),  throwName( t, N ), P+t );
    for( int i=0; i<nBottom; ++i, ++t ) addPin( 270, QPoint( rowOffset( i, nBottom ),  end ),  throwName( t, N ), P+t );

    m_contact.resize( T );
    m_ePin.resize( 2*T );
    for( int c=0; c<T; ++c )
    {
        QString cid = m_id + "-contact" + QString::number( c );
        m_contact[c]  = new eResistor( cid );
        m_ePin[2*c]   = new ePin( cid+"pinP", 1 );
        m_ePin[2*c+1] = m_pin[P+c];                // The throw's pin is its ePin
        m_contact[c]->setEpin( 0, m_ePin[2*c] );
        m_contact[c]->setEpin( 1, m_ePin[2*c+1] );
    }
    QSignalBlocker blocker( m_knob );
    m_knob->setRange( 0, N-1 );
    m_knob->setStartAngle( -(N-1)*15.0 );          // The sweep is centred on 12 o'clock
    m_knob->setValue( m_position );

    rest();
    showPosition();
    placeLabels();
    Circuit::self()->update();
}

bool RotarySwitch::isClosed( int pole, int pos )
{
    return m_contact[ pole*m_positions + pos ]->admit() > 0;
}

void RotarySwitch::setClosed( int pole, int pos, bool closed )
{
    m_contact[ pole*m_positions + pos ]->setAdmit( closed ? m_onAdmit : 0 );
}

//--- Running -----------------------------------------------------------------

void RotarySwitch::initialize()
{
    m_started    = false;
    m_mode       = modeNone;
    m_bounceLeft = 0;
    for( int p=0; p<4; ++p ) m_chatter[p] = -1;
}

void RotarySwitch::stamp()
{
    for( int p=0; p<m_poles; ++p )                 // Every contact of a pole starts from its common
    {
        eNode* enode = m_pin[p]->getEnode();
        for( int k=0; k<m_positions; ++k ) m_ePin[ 2*(p*m_positions+k) ]->setEnode( enode );
    }
    rest();
    m_started = true;                              // From here on a step has its timing
}

void RotarySwitch::updateStep()
{
    if( !m_changed ) return;
    m_changed = false;

    if( m_started && Simulator::self()->isRunning() && m_position != m_at ) moveTo( m_position );
    else rest();                                   // Stopped, or before the run's first step: at once
    update();
}

void RotarySwitch::rest()
{
    Simulator::self()->cancelEvents( this );
    m_mode = modeNone;
    m_bounceLeft = 0;

    for( int p=0; p<m_poles; ++p )
    {
        m_chatter[p] = -1;
        for( int k=0; k<m_positions; ++k ) setClosed( p, k, k == m_position );
    }
    m_at = m_position;
}

void RotarySwitch::moveTo( int pos )
{
    Simulator::self()->cancelEvents( this );       // A step still under way is cut short
    m_mode = modeNone;

    double delay = 0;
    bool pending = false;

    if( m_shorting )                               // Make before break: pos closes now, the rest opens after the overlap
    {
        for( int p=0; p<m_poles; ++p )
        {
            m_chatter[p] = -1;
            for( int k=0; k<m_positions; ++k )
            {
                bool leaving = ( k != pos ) && isClosed( p, k ) && m_chatter[p] == -1;
                if( leaving ) m_chatter[p] = k;    // The contact that was made: it breaks with a bounce
                setClosed( p, k, leaving );
            }
            setClosed( p, pos, true );
            if( m_chatter[p] >= 0 ) pending = true;
        }
        m_endClosed = false;
        delay = m_overlap;
    }else{                                         // Break before make: all open now, pos makes after the transit
        for( int p=0; p<m_poles; ++p )
        {
            for( int k=0; k<m_positions; ++k ) setClosed( p, k, false );
            m_chatter[p] = pos;
        }
        m_endClosed = true;
        pending = true;
        delay = m_transit;
    }
    m_at = pos;
    if( !pending ) return;

    if( delay > 0 )
    {
        m_mode = modeWait;
        uint64_t ps = (uint64_t)( delay*1e12 );
        Simulator::self()->addEvent( ps ? ps : 1, this );
    }
    else arrive();
}

void RotarySwitch::arrive()                       // The wait is over: the contact that changes is closed, and chatters
{
    m_isClosed = true;
    for( int p=0; p<m_poles; ++p )
        if( m_chatter[p] >= 0 ) setClosed( p, m_chatter[p], true );

    if( m_bounceTime <= 0 ){ finishBounce(); return; }

    m_bounceLeft = m_endClosed ? 2*m_bounces       // An even number of changes ends as it started: closed
                               : 2*m_bounces + 1;  // An odd one ends open
    m_bounceN    = 0;
    m_mode       = modeBounce;
    nextBounce();
}

// Gaps shrink by 0.7 each change (the contact's swing dies down) and add up to Bounce_Time
void RotarySwitch::nextBounce()
{
    double r = 0.7, sum = ( 1-qPow( r, m_bounceLeft + m_bounceN ) )/( 1-r );
    double gap = m_bounceTime*qPow( r, m_bounceN )/sum;        // Seconds
    uint64_t ps = (uint64_t)( gap*1e12 );
    if( ps < 1 ) ps = 1;
    Simulator::self()->addEvent( ps, this );
}

void RotarySwitch::runEvent()
{
    if( m_mode == modeWait ){ arrive(); return; }
    if( m_mode != modeBounce ) return;

    m_bounceLeft--;
    m_bounceN++;
    m_isClosed = !m_isClosed;
    for( int p=0; p<m_poles; ++p )
        if( m_chatter[p] >= 0 ) setClosed( p, m_chatter[p], m_isClosed );

    if( m_bounceLeft ) nextBounce();
    else finishBounce();
}

void RotarySwitch::finishBounce()
{
    for( int p=0; p<m_poles; ++p )
    {
        if( m_chatter[p] >= 0 ) setClosed( p, m_chatter[p], m_endClosed );
        m_chatter[p] = -1;
    }
    m_mode = modeNone;
    m_bounceLeft = 0;
}

//--- From the user -----------------------------------------------------------

void RotarySwitch::dialChanged( int v )
{
    if( v == m_position ) return;

    m_position = v;
    m_changed = true;
    showPosition();
    if( !Simulator::self()->isRunning() ) updateStep();
}

void RotarySwitch::step( int dir )
{
    int v = qBound( 0, m_position + dir, m_positions-1 );
    if( v != m_position ) m_knob->setValue( v );   // dialChanged() follows
}

void RotarySwitch::showPosition()
{
    if( m_showProperty == "Position" ) setValLabelText( QString::number( m_position + 1 ) );
}

//--- Keyframes ---------------------------------------------------------------

void RotarySwitch::simState( StateIO& s )
{
    eElement::simState( s );
    s.io( m_position );
    s.io( m_at );
    s.io( m_mode );
    s.io( m_started );
    s.io( m_endClosed );
    s.io( m_isClosed );
    s.io( m_chatter );
    s.io( m_bounceLeft );
    s.io( m_bounceN );
}

void RotarySwitch::stateRestored()
{
    QSignalBlocker blocker( m_knob );
    m_knob->setValue( m_position );
    showPosition();
    update();
}

void RotarySwitch::remove()
{
    for( eResistor* c : m_contact ) delete c;
    m_contact.clear();
    QObject::disconnect( m_keyEventConn );
    Component::remove();
}

//--- Looks -------------------------------------------------------------------

void RotarySwitch::setAngle( double angle )
{
    RotarySwitch::rotateAngle( angle );
}

// The knob stays in the middle of the body, upright and unmirrored, however the part is turned
// or flipped: the rules of placeLabels()
void RotarySwitch::placeKnob()
{
    m_proxy->setRotation( -rotation()*m_Hflip*m_Vflip );
    m_proxy->setTransform( QTransform::fromScale( m_Hflip, m_Vflip ) );
    m_proxy->setPos( -20*m_Hflip, -20*m_Vflip );
}

void RotarySwitch::rotateAngle( double a )
{
    Component::rotateAngle( a );                   // ... and moveSignal(): the names
    placeKnob();
}

void RotarySwitch::setflip()
{
    Component::setflip();                         // ... and moveSignal(): the names
    placeKnob();
}

// After the pins put their own labels back (a move, a turn, a flip): ours again
void RotarySwitch::moveSignal()
{
    Component::moveSignal();
    placeLabels();
}

void RotarySwitch::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );                   // Pen and brush from the theme
    p->setRenderHint( QPainter::Antialiasing, true );
    QPen   pen   = p->pen();
    QBrush brush = p->brush();
    qreal inner = m_radius;                        // Where the pins end at the body

    for( Pin* pin : m_pin )                        // From the circle to the pins that stand off-centre
    {
        int  angle = pin->pinAngle();
        bool side  = ( angle % 180 == 0 );         // On the left or on the right
        int  d     = qRound( side ? pin->pos().y() : pin->pos().x() );
        qreal reach = sqrt( double( m_radius*m_radius ) - d*d );   // Where the circle lies on the pin's line
        if( reach > inner - 1 ) continue;

        if( side ) p->drawLine( QPointF( ( angle == 0 ) ? reach : -reach, d )
                              , QPointF( ( angle == 0 ) ? inner  : -inner,  d ) );
        else       p->drawLine( QPointF( d, ( angle == 90 ) ? -reach : reach )
                              , QPointF( d, ( angle == 90 ) ? -inner  : inner ) );
    }
    p->setPen( pen );
    p->setBrush( brush );
    p->drawEllipse( QRectF( -m_radius, -m_radius, 2*m_radius, 2*m_radius ) );

    Component::paintSelected( p );
}
