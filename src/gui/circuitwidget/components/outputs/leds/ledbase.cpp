/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>

#include "ledbase.h"
#include "theme.h"
#include "circuitwidget.h"
#include "connector.h"
#include "simulator.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"
#include "stringprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("LedBase",str)

eNode LedBase::m_gndEnode("");
int   LedBase::m_overBright = 0;
QString LedBase::getColorList() {
    return "Yellow,Red,Green,Blue,Orange,Purple,White;"
           +tr("Yellow")+","+tr("Red")+","+tr("Green")+","+tr("Blue")
          +","+tr("Orange")+","+tr("Purple")+","+tr("White");}

LedBase::LedBase( QString type, QString id )
       : Component( type, id )
       , eLed( id )
{
    m_graphical = true;
    m_grounded  = false;
    m_intensity = 0;
    m_peakLimit = 0;
    m_stepCur = m_avgCur = m_peakCur = 0;
    m_lastT = m_peakT = 0;
    m_peaked = false;

    m_color = QColor( Qt::black );
    setColorStr("Yellow");

    Simulator::self()->addToUpdateList( this );

    addPropGroup( { tr("Main"), {
        new StrProp <LedBase>("Color", tr("Color"), getColorList()
                             , this, &LedBase::colorStr, &LedBase::setColorStr,0,"enum" ),

        new BoolProp<LedBase>("Grounded", tr("Grounded"), ""
                             , this, &LedBase::grounded, &LedBase::setGrounded, propNoCopy ),
        showStateProp()
    }, 0} );
    enableStates( false );   // On request only: circuits often have many LEDs

    addPropGroup( { tr("Electric"), {
        new DoubProp<LedBase>("Threshold", tr("Forward Voltage"), "V"
                             , this, &LedBase::threshold, &LedBase::setThreshold ),

        new DoubProp<LedBase>("MaxCurrent", tr("Max Current (IF, average)"), "mA"
                             , this, &LedBase::maxCurrent, &LedBase::setMaxCurrent ),

        new DoubProp<LedBase>("PeakCurrent", tr("Peak Current (IFP, 0 = none)"), "mA"
                             , this, &LedBase::peakCurrentRating, &LedBase::setPeakCurrentRating ),

        new DoubProp<LedBase>("Resistance", tr("Resistance"), "Ω"
                             , this, &LedBase::res, &LedBase::setRes ),
    }, 0} );

    addPropGroup( { tr("Capacitance"), {
        new DoubProp<LedBase>("Cj0", tr("Junction Capacitance at 0 V"), "pF"
                          , this, &LedBase::cj0, &LedBase::setCj0 ),
        new DoubProp<LedBase>("Vj", tr("Junction Potential"), "V"
                          , this, &LedBase::vj, &LedBase::setVj ),
        new DoubProp<LedBase>("Mj", tr("Grading Coefficient"), ""
                          , this, &LedBase::mj, &LedBase::setMj )
    },0} );
}
LedBase::~LedBase(){}

void LedBase::initialize()
{
    m_crashed = false;
    m_warning = false;

    if( m_grounded ) m_ePin[1]->setEnode( &m_gndEnode );

    eLed::initialize();
    m_region = -1;
    clearStates();
    m_stepCur = m_avgCur = m_peakCur = 0;
    m_lastT = m_peakT = 0;
    m_peaked = false;
    update();
}

// Ratings as in a datasheet (as the diode's, docs/PART_STATES.md). Max Current is IF, the current
// the LED carries for good (a heating limit): compared with I through a first-order low-pass of
// 100 ms, so a 100 Hz ripple or a PWM drive is smoothed. Peak Current is IFP, the pulse rating:
// compared with the current of each solved step (0 = none, the default). Before, the state
// compared the current at that moment with Max Current: the LED strip of led_driver board_mains
// (312 mA average, 550 mA peaks at 100 Hz on 5 x 60 mA strings) read OVER on every mains crest.
// Flags only: the LED is not damaged, the circuit goes on as before.
#define AVG_TAU   100e9   // ps
#define PEAK_HOLD 100e9   // ps: PEAK shown this long after the last step above IFP

inline void LedBase::average( uint64_t t )   // I held since m_lastT, into the low-pass
{
    if( t <= m_lastT ) return;
    double x = (double)(t - m_lastT)*( 1/AVG_TAU );
    double k = ( x < 0.01 ) ? x : 1-std::exp( -x );   // Steps are far shorter than tau: no exp()
    m_avgCur += ( m_stepCur - m_avgCur )*k;
    m_lastT = t;
}

inline void LedBase::setRegion( uint64_t t )   // average( t ) done
{
    if( m_peakLimit > 0 && m_stepCur > m_peakLimit ){ m_peaked = true; m_peakT = t; }  // Still above
    else if( m_peaked && t - m_peakT > PEAK_HOLD ) m_peaked = false;

    double i = m_stepCur;
    int r = m_region;
    if( m_peaked ) r = 3;
    else if( m_maxCurrent > 0 && ( r == 2 ? m_avgCur > 0.95*m_maxCurrent : m_avgCur > m_maxCurrent ) ) r = 2;
    else if( r >= 1 ) r = ( i < 5e-6 ) ? 0 : 1;
    else              r = ( i > 1e-5 ) ? 1 : 0;

    if( r == m_region ) return;
    m_region = r;
    if     ( r == 0 ) reportState("OFF",  stOff );
    else if( r == 1 ) reportState("ON",   stOn );
    else if( r == 2 ) reportState("OVER", stFault );
    else              reportState("PEAK", stFault );
}

void LedBase::updateStep()   // GUI thread, worker idle
{
    uint32_t intensity = m_intensity;
    eLed::updateBright();

    // Nothing changed since the last solved step (a DC circuit): the average still moves and PEAK
    // still ends, so the state is updated here too, with the current held since then
    uint64_t t = Simulator::self()->circTime();
    average( t );
    setRegion( t );

    bool warning = m_peaked || ( m_maxCurrent > 0 && m_avgCur > m_maxCurrent );
    bool crashed = m_maxCurrent > 0 && m_avgCur > 2*m_maxCurrent;
    if( warning || crashed || m_warning || m_crashed ) update();
    m_warning = warning || crashed;
    m_crashed = crashed;

    if( intensity != m_intensity ) update();
    if( m_changed )
    {
        m_changed = false;
        voltChanged();
    }
}

void LedBase::setGrounded( bool grounded )
{
    if( grounded == m_grounded ) return;
    m_grounded = grounded;

    if( Simulator::self()->isRunning() )  CircuitWidget::self()->powerCircOff();

    Pin* pin1 = static_cast<Pin*>(m_ePin[1]);
    pin1->setEnabled( !grounded );
    pin1->setVisible( !grounded );
    if( grounded ) pin1->removeConnector();
    else           pin1->setEnode( NULL );
}

void LedBase::setColorStr( QString color )
{
    m_ledColorStr = color;
    double thr = 2.4;

    if     ( color == "Red"    || color == "1") { thr = 1.8; m_ledColor = red; }
    else if( color == "Green"  || color == "2") { thr = 3.5; m_ledColor = green; }
    else if( color == "Blue"   || color == "3") { thr = 3.6; m_ledColor = blue; }
    else if( color == "Orange" || color == "4") { thr = 2.0; m_ledColor = orange; }
    else if( color == "Purple" || color == "5") { thr = 3.5; m_ledColor = purple; }
    else if( color == "White"  || color == "6") { thr = 4.0; m_ledColor = white; }
    else                                        { thr = 2.4; m_ledColor = yellow; } // Yellow and undefined

    eLed::setThreshold( thr );

    if( m_showVal && (m_showProperty == "Color") )
        setValLabelText( color );
}

QColor LedBase::getColor( ledColor_t c, int bright )
{
    m_overBright = 0;
    int secL = bright/3;
    int secH = bright/2;
    int secX = bright*2/3;
    QColor foreColor;

    if( bright > 255 )
    {
        m_overBright = (bright-255);
        secL += m_overBright;
        bright = 255;
    }
    switch( c ) {
        case yellow: foreColor = QColor( bright, bright, secL ); break;
        case red:    foreColor = QColor( bright, secH  , secH ); break;
        case green:  foreColor = QColor( secL  , bright, secL ); break;
        case blue:   foreColor = QColor( secH  , secH  , bright ); break;
        case orange: foreColor = QColor( bright, secX  , secL ); break;
        case purple: foreColor = QColor( bright, secL  , bright ); break;
        case white:  foreColor = QColor( bright, bright, bright ); break;
    }
    return foreColor;
}

void LedBase::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    // Body in the theme's body colour (white on light canvases, black on dark ones), LED symbols
    // outlined in the line colour and filled with the LED colour as it lights up, so an LED is
    // recognisable when off on any canvas. Above Max Current the body glows (over-bright); far
    // above it (crashed) the body turns white with red symbols
    QColor body = Theme::body();
    QColor full = getColor( m_ledColor, 255 );
    getColor( m_ledColor, m_intensity );                 // Sets m_overBright
    double t = qMin( m_intensity, (uint32_t)255 )/255.0;
    auto mix = [t]( int a, int b ){ return int( a + ( b-a )*t ); };
    QColor fill( mix( body.red(), full.red() ), mix( body.green(), full.green() ), mix( body.blue(), full.blue() ) );
    QColor symbolLine = Theme::line();

    if( m_crashed )                                      // Extreme over current
    {
        body = Qt::white;
        fill = Qt::white;
        symbolLine = Qt::red;
    }
    else if( m_overBright > 0 )                          // Over-bright: body glows with the excess
    {
        int over = qMin( m_overBright*2, 255 );
        auto glow = [over]( int a, int b ){ return a + ( b-a )*over/255; };
        body = QColor( glow( body.red(), full.red() ), glow( body.green(), full.green() ), glow( body.blue(), full.blue() ) );
    }
    if( m_warning && !m_crashed ) symbolLine = Qt::red;  // Over current

    QPen ring( Theme::line(), 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );   // Body outline
    p->setPen( ring );
    p->setBrush( body );
    drawBackground( p );

    QPen pen( symbolLine, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );
    p->setPen( pen );
    p->setBrush( fill );
    drawForeground( p );

    Component::paintSelected( p );
}

void LedBase::voltChanged()
{
    Simulator::self()->addToSettled( this );   // Ratings and state from this step's final values
    eLed::voltChanged();
}

// Part state (docs/PART_STATES.md): OFF, ON (green: lit, conducting more than 10 uA, back below
// 5 uA), OVER (red: the averaged current, 100 ms low-pass, above Max Current, IF; back below 0.95 x)
// or PEAK (red: a solved step above Peak Current, IFP; held 100 ms after the last one).
void LedBase::stepSettled()   // Worker: a solved step
{
    uint64_t t = Simulator::self()->circTime();
    average( t );
    double i = m_current;
    if( m_peakLimit > 0 && m_stepCur > m_peakLimit ) m_peakT = t;   // The held current lasted until now
    m_stepCur = i;
    if( i > m_peakCur ) m_peakCur = i;
    setRegion( t );
}
