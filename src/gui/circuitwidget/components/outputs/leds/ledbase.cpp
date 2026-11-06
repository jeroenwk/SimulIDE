/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

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

        new DoubProp<LedBase>("MaxCurrent", tr("Max Current"), "mA"
                             , this, &LedBase::maxCurrent, &LedBase::setMaxCurrent ),

        new DoubProp<LedBase>("Resistance", tr("Resistance"), "Ω"
                             , this, &LedBase::res, &LedBase::setRes ),
    }, 0} );
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
    update();
}

void LedBase::updateStep()
{
    uint32_t intensity = m_intensity;
    eLed::updateBright();

    if( overCurrent() > 1.5 )
    {
        m_warning = true;
        m_crashed = overCurrent() > 2;
        update();
    }else{
        if( m_warning ) update();
        m_warning = false;
        m_crashed = false;
    }
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
    if( m_stateLog ) Simulator::self()->addToSettled( this );   // State from this step's final values
    eLed::voltChanged();
}

// Part state (docs/PART_STATES.md): OFF, ON (green: lit, conducting more than 10 uA, back below 5 uA)
// or OVER (red: above Max Current at this moment, back below 0.95 x). The red crash drawing still
// uses the averaged current (1.5 x): a PWM-driven LED can show OVER on its pulses without crashing.
void LedBase::stepSettled()
{
    double i = m_current;
    int r = m_region;
    if( m_maxCurrent > 0 && ( r == 2 ? i > 0.95*m_maxCurrent : i > m_maxCurrent ) ) r = 2;
    else if( r == 1 || r == 2 ) r = ( i < 5e-6 ) ? 0 : 1;
    else r = ( i > 1e-5 ) ? 1 : 0;

    if( r == m_region ) return;
    m_region = r;
    if     ( r == 0 ) reportState("OFF",  stOff );
    else if( r == 1 ) reportState("ON",   stOn );
    else              reportState("OVER", stFault );
}
