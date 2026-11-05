/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>

#include "ledstrip.h"
#include "pin.h"
#include "itemlibrary.h"
#include "theme.h"

#include "doubleprop.h"
#include "stringprop.h"
#include "intprop.h"

#define tr(str) simulideTr("LedStrip",str)

Component* LedStrip::construct( QString type, QString id )
{ return new LedStrip( type, id ); }

LibraryItem* LedStrip::libraryItem()
{
    return new LibraryItem(
        tr("Led Strip"),
        "Leds",
        "ledstrip.png",
        "LedStrip",
        LedStrip::construct );
}

LedStrip::LedStrip( QString type, QString id )
        : LedBase( type, id )
{
    m_area = QRect(-26,-10, 52, 20 );

    m_pin.resize( 2 );
    m_pin[0] = new Pin( 180, QPoint(-32, 0 ), m_id+"-lPin", 0, this ); // Anode
    m_pin[1] = new Pin(   0, QPoint( 32, 0 ), m_id+"-rPin", 1, this ); // Cathode
    setEpin( 0, m_pin[0] );
    setEpin( 1, m_pin[1] );

    m_count   = 12;
    m_strings = 1;
    m_ledVf   = 2.75;
    m_ledRes  = 1.0;
    m_ledImax = 0.3;

    // Replace LedBase's groups: here the electric values are per LED, the totals follow
    for( QString g : { simulideTr("LedBase","Main"), simulideTr("LedBase","Electric") } )
    {
        propGroup* pg = getPropGroup( g );
        if( !pg ) continue;
        QList<ComProperty*> props = pg->propList;
        remPropGroup( g );
        qDeleteAll( props );
    }
    addPropGroup( { tr("Main"), {
        new StrProp<LedStrip>("Color", tr("Color"), LedBase::getColorList()
                             , this, &LedStrip::color, &LedStrip::setColor, 0, "enum" ),

        new IntProp<LedStrip>("Count", tr("LEDs in series"), "_LEDs"
                             , this, &LedStrip::count, &LedStrip::setCount ),

        new IntProp<LedStrip>("Strings", tr("Strings in parallel"), "_Strings"
                             , this, &LedStrip::strings, &LedStrip::setStrings ),

        new StrProp<LedStrip>("Totals", tr("Whole strip"), ""
                             , this, &LedStrip::totals, &LedStrip::setTotals, propNoCopy ),
        showStateProp()
    }, 0} );

    addPropGroup( { tr("Per LED"), {
        new DoubProp<LedStrip>("LedVf", tr("Forward Voltage"), "V"
                              , this, &LedStrip::ledVf, &LedStrip::setLedVf ),

        new DoubProp<LedStrip>("LedRes", tr("Resistance"), "Ω"
                              , this, &LedStrip::ledRes, &LedStrip::setLedRes ),

        new DoubProp<LedStrip>("LedImax", tr("Max Current"), "mA"
                              , this, &LedStrip::ledImax, &LedStrip::setLedImax ),
    }, 0} );

    setLabelPos(-24,-28, 0 );
    setValLabelPos(-24, 12, 0 );
    setColor("White");
    setShowProp("Totals");
}
LedStrip::~LedStrip(){}

void LedStrip::initialize()
{
    LedBase::initialize();
    if( showProp() == "Totals" ) setValLabelText( totals() );
}

void LedStrip::setColor( QString c )
{
    LedBase::setColorStr( c );   // Also sets a single LED's forward voltage: put the strip's back
    updateModel();
}

void LedStrip::setCount( int n )      { if( n < 1 ) n = 1; m_count = n;   updateModel(); }
void LedStrip::setStrings( int m )    { if( m < 1 ) m = 1; m_strings = m; updateModel(); }
void LedStrip::setLedVf( double v )   { if( v < 0 ) v = 0; m_ledVf = v;   updateModel(); }
void LedStrip::setLedRes( double r )  { if( r <= 0 ) r = 1e-3; m_ledRes = r; updateModel(); }
void LedStrip::setLedImax( double i ) { if( i <= 0 ) i = 1e-3; m_ledImax = i; updateModel(); }

void LedStrip::updateModel()
{
    eLed::setThreshold( m_count*m_ledVf );
    eLed::setMaxCurrent( m_strings*m_ledImax );
    eLed::setRes( m_count*m_ledRes/m_strings );
    if( showProp() == "Totals" ) setValLabelText( totals() );
    update();
}

QString LedStrip::totals()
{
    QString s = QString::number( m_count )+"× ";
    if( m_strings > 1 ) s = QString::number( m_strings )+" × "+s;
    return s + QString::number( m_count*m_ledVf, 'g', 4 )+" V "
             + QString::number( m_count*m_ledRes/m_strings, 'g', 4 )+" Ω";
}

void LedStrip::drawBackground( QPainter* p )
{
    p->drawRoundedRect( QRectF(-24,-8, 48, 16 ), 3, 3 );
}

void LedStrip::drawForeground( QPainter* p )
{
    // Filled with the glow colour (the brush LedBase set: black when off), outlined in the
    // line colour so the three LED symbols stay visible on the dark body when the strip is off
    QPen outline( Theme::line(), 1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin );

    p->setPen( outline );
    p->drawLine(-26, 0, 26, 0 );
    for( int x : { -14, 0, 14 } )     // Three LED symbols stand for the string
    {
        const QPointF tri[3] = { QPointF( x+4, 0 ), QPointF( x-4,-4 ), QPointF( x-4, 4 ) };
        p->drawPolygon( tri, 3 );
        p->drawLine( x+4,-4, x+4, 4 );
    }
}
