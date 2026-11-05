/***************************************************************************
 *   Circuit colours: canvas, wires, component outlines and text           *
 ***( see copyright.txt file at root folder )*******************************/

#include <QSettings>

#include <cmath>
#include <utility>

#include "theme.h"
#include "mainwindow.h"
#include "circuit.h"
#include "component.h"
#include "pin.h"
#include "circuitwidget.h"
#include "appdialog.h"

bool   Theme::m_loaded = false;
QColor Theme::m_color[Theme::numRoles];
QColor Theme::m_saved[Theme::numRoles];
bool   Theme::m_pushed = false;

QColor Theme::defaultColor( role_t r )
{
    switch( r ){
        case canvasRole: return QColor( 240, 240, 210 );
        case wireRole:   return QColor(  40,  40,  60 );
        case lineRole:   return QColor(   0,   0,   0 );
        case textRole:   return QColor(   0,   0,   0 );
        case bodyRole:   return QColor( 255, 255, 255 );
        case flowRole:   return QColor( 255, 190,   0 );
        case nameRole:   return QColor(   0,   0, 128 );   // Qt::darkBlue
        case valueRole:  return QColor( 128,   0,   0 );   // Qt::darkRed
        case animHighRole: return QColor( 200,  50,  50 );
        case animLowRole:  return QColor(  50,  50, 200 );
        case outHighRole:  return QColor( 180,   0,   0 );
        case outLowRole:   return QColor(   0,   0, 180 );
        case inHighRole:   return QColor( 250, 120,   0 );
        case inLowRole:    return QColor( 100, 100, 250 );
        case openHighRole: return QColor(  60, 120,  60 );
        default:         return QColor();
    }
}

QString Theme::key( role_t r )
{
    static const char* keys[] = { "canvasColor", "wireColor", "lineColor", "textColor", "bodyColor", "flowColor", "nameColor", "valueColor",
        "animHighColor", "animLowColor", "outHighColor", "outLowColor", "inHighColor", "inLowColor", "openHighColor" };
    return keys[r];
}

QString Theme::caption( role_t r )
{
    static const char* caps[] = { "Canvas", "Wires", "Outlines", "Text", "Bodies", "Current dots", "Names", "Values",
        "Wire high", "Wire low", "Output high", "Output low", "Input high", "Input low", "Open high" };
    return caps[r];
}

void Theme::load()
{
    m_loaded = true;
    QSettings* s = MainWindow::self() ? MainWindow::self()->settings() : nullptr;
    for( int i=0; i<numRoles; ++i )
    {
        role_t r = (role_t)i;
        m_color[i] = defaultColor( r );
        if( s && s->contains("Circuit/"+key( r ) ) )
        {
            QColor c( s->value("Circuit/"+key( r ) ).toString() );
            if( c.isValid() ) m_color[i] = c;
        }
    }
}

QColor Theme::get( role_t r )
{
    if( !m_loaded ) load();
    return m_color[r];
}

void Theme::set( role_t r, QColor c )
{
    if( !c.isValid() ) return;
    if( !m_loaded ) load();
    m_color[r] = c;
    if( MainWindow::self() ) MainWindow::self()->settings()->setValue("Circuit/"+key( r ), c.name() );
    apply();
}

void Theme::reset()
{
    if( !m_loaded ) load();
    for( int i=0; i<numRoles; ++i )
    {
        m_color[i] = defaultColor( (role_t)i );
        if( MainWindow::self() ) MainWindow::self()->settings()->remove("Circuit/"+key( (role_t)i ) );
    }
    apply();
}

static double luminance( QColor c )
{
    auto lin = []( double v ){ v /= 255; return v <= 0.03928 ? v/12.92 : pow( (v+0.055)/1.055, 2.4 ); };
    return 0.2126*lin( c.red() ) + 0.7152*lin( c.green() ) + 0.0722*lin( c.blue() );
}

double Theme::contrast( QColor a, QColor b )
{
    double la = luminance( a ), lb = luminance( b );
    if( la < lb ) std::swap( la, lb );
    return ( la+0.05 )/( lb+0.05 );
}

QColor Theme::lead( QColor c )
{
    QColor bg = canvas();
    bool lightBg = bg.lightness() > 128;

    if( c.hslSaturation() < 20 )                 // Neutral lead: a grey well apart from the wires
    {
        int w = wire().lightness();
        int g = ( w > 128 ) ? w-60 : w+70;
        c = QColor::fromHsl( 0, 0, qBound( 0, g, 255 ) );
    }
    int h = c.hslHue(), s = c.hslSaturation(), l = c.lightness();
    for( int i=0; i<40 && contrast( c, bg ) < 3.0; ++i ) // Darken on light, lighten on dark canvases
    {
        l = qBound( 0, l + ( lightBg ? -5 : 5 ), 255 );
        c = QColor::fromHsl( h, s, l );
    }
    return c;
}

QColor Theme::grid()
{
    QColor c = canvas();
    if( c == defaultColor( canvasRole ) ) return QColor( 210, 210, 210 ); // Classic grid
    // A bit toward the opposite end, so it works for black and white canvases too
    double f = ( c.lightness() > 128 ) ? -0.12 : 0.12;
    auto mix = [f]( int v ){ return qBound( 0, int( v + ( f > 0 ? (255-v)*f : v*f ) ), 255 ); };
    return QColor( mix( c.red() ), mix( c.green() ), mix( c.blue() ) );
}

void Theme::apply()
{
    Circuit* circ = Circuit::self();
    if( !circ ) return;
    for( Component* comp : *circ->compList() )
    {
        comp->updateLabelColors();
        for( Pin* pin : comp->getPins() ) if( pin ) pin->updateLabelColor();
    }
    circ->update();
    if( CircuitWidget::self() && CircuitWidget::self()->appDialog() )
        CircuitWidget::self()->appDialog()->refreshColors();   // Settings dialog shows the new colours
}

// Canvas, wires, outlines, text, bodies, current dots, names, values,
// then animate: wire high/low, output high/low, input high/low, open high
namespace {
struct Preset { const char* name; const char* colors[Theme::numRoles]; };
const Preset presetList[] = {
    { "Classic", { "#f0f0d2", "#28283c", "#000000", "#000000", "#ffffff", "#ffbe00", "#000080", "#800000",
                  "#c83232", "#3232c8", "#b40000", "#0000b4", "#fa7800", "#6464fa", "#3c783c" } },
    { "Light",   { "#ffffff", "#3c3c50", "#000000", "#000000", "#ffffff", "#ff8c00", "#000080", "#800000",
                  "#c83232", "#3232c8", "#b40000", "#0000b4", "#fa7800", "#6464fa", "#3c783c" } },
    // Dark, inspired by EveryCircuit: black canvas, hollow light grey parts, pale green current
    { "Dark",    { "#000000", "#9e9e9e", "#b4b4b4", "#c8c8c8", "#000000", "#b9e3b0", "#9db8e0", "#c8c8c8",
                  "#ff6060", "#6f8fff", "#ff6b6b", "#6f8fff", "#ffae42", "#a3b1ff", "#7fd17f" } },
};
}

QStringList Theme::presets()
{
    QStringList list;
    for( const Preset& p : presetList ) list.append( p.name );
    return list;
}

bool Theme::applyPreset( QString name )
{
    for( const Preset& p : presetList )
    {
        if( name.compare( p.name, Qt::CaseInsensitive ) != 0 ) continue;
        if( !m_loaded ) load();
        for( int i=0; i<numRoles; ++i )
        {
            m_color[i] = QColor( p.colors[i] );
            if( MainWindow::self() ){
                QSettings* s = MainWindow::self()->settings();
                if( m_color[i] == defaultColor( (role_t)i ) ) s->remove("Circuit/"+key( (role_t)i ) );
                else s->setValue("Circuit/"+key( (role_t)i ), m_color[i].name() );
            }
        }
        apply();
        return true;
    }
    return false;
}

bool Theme::pushPreset( QString name )
{
    for( const Preset& p : presetList )
    {
        if( name.compare( p.name, Qt::CaseInsensitive ) != 0 ) continue;
        if( !m_loaded ) load();
        if( !m_pushed ) for( int i=0; i<numRoles; ++i ) m_saved[i] = m_color[i];
        m_pushed = true;
        for( int i=0; i<numRoles; ++i ) m_color[i] = QColor( p.colors[i] );
        apply();
        return true;
    }
    return false;
}

void Theme::popColors()
{
    if( !m_pushed ) return;
    m_pushed = false;
    for( int i=0; i<numRoles; ++i ) m_color[i] = m_saved[i];
    apply();
}

QString Theme::currentPreset()
{
    for( const Preset& p : presetList )
    {
        bool same = true;
        for( int i=0; i<numRoles && same; ++i ) same = ( get( (role_t)i ) == QColor( p.colors[i] ) );
        if( same ) return p.name;
    }
    return "Custom";
}

QColor Theme::pinState( int state, QColor undefColor )
{
    switch( state ){                     // pinState_t (pin.h)
        case 1: return get( inLowRole );    // driven_low: open high, driven low externally
        case 2: return get( outLowRole );   // open_low
        case 3: return get( openHighRole ); // open_high
        case 4: return get( outLowRole );   // out_low
        case 5: return get( outHighRole );  // out_high
        case 6: return get( inLowRole );    // input_low
        case 7: return get( inHighRole );   // input_high
        default: return undefColor;         // undef_state
    }
}
