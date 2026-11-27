/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                  *
 *                                                                         *
 *   Power Meter: a Meter in the lead of a source that shows the power     *
 *   through it, with the r.m.s. voltage, the r.m.s. current and the power *
 *   factor (docs/POWER_METER.md)                                           *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>
#include <QFont>
#include <QPainter>
#include <cmath>
#include <limits>

#include "powermeter.h"
#include "e-pin.h"
#include "e-node.h"
#include "iopin.h"
#include "itemlibrary.h"
#include "theme.h"
#include "appfonts.h"
#include "simulator.h"

#define tr(str) simulideTr("PowerMeter",str)

Component* PowerMeter::construct( QString type, QString id )
{ return new PowerMeter( type, id ); }

LibraryItem* PowerMeter::libraryItem()
{
    return new LibraryItem(
        tr("Power Meter"),
        "Meters",
        "powermeter.png",
        "PowerMeter",
        PowerMeter::construct);
}

PowerMeter::PowerMeter( QString type, QString id )
           : Meter( type, id, false )      // There is no r.m.s. of a power: Instant or Mean
{
    m_area = QRectF( -42, -52, 84, 60 );    // The ammeter's pins (it goes in their place), a taller face
    m_unit = "W";
    setRes( 1e-6 );                        // As the ammeter: a short in the lead

    QFont f( AppFonts::sans(), 10, QFont::Bold );
    f.setPixelSize(11);
    m_display.setFont(f);
    m_display.setText( " 0.000 W\n  0.000 V\n  0.000 A\nPF  ----" );
    setflip();
}

PowerMeter::~PowerMeter(){}

// The red lead's voltage to ground, and the current into it: their product is the power that the
// red side delivers to the black side, the return being ground (docs/POWER_METER.md)
double PowerMeter::instant() { return m_ePin[0]->getVoltage() * currentThrough(); }

double PowerMeter::measured()
{
    Simulator* sim = Simulator::self();
    if( m_reading == rInstant || ( !sim->isRunning() && !sim->isPaused() ) ) return instant();
    double v, i, p;
    valuesAt( sim->circTime(), v, i, p );
    return p;
}

void PowerMeter::initialize()
{
    Meter::initialize();
    m_pmBlockStart = m_pmLastT = 0;
    m_sumV2 = m_sumI2 = m_sumP = 0;
    m_vRms = m_iRms = m_pMean = 0;
    m_heldV = m_heldI = 0;
    m_pmHaveResult = false;
}

void PowerMeter::stamp()
{
    Meter::stamp();
    restartAt( Simulator::self()->circTime() );
    m_ePin[0]->changeCallBack( this );      // V and I are integrated in every reading
    m_ePin[1]->changeCallBack( this );
}

void PowerMeter::setReading( QString r )
{
    if( r.trimmed().toLower() == "rms" ) r = "Mean";
    Meter::setReading( r );
    Simulator* sim = Simulator::self();
    if( !sim->isRunning() ) return;         // Started from stamp()
    sim->pauseSim();                        // Changed during a run: from now
    restartAt( sim->circTime() );
    m_ePin[0]->changeCallBack( this );
    m_ePin[1]->changeCallBack( this );
    sim->resumeSim();
}

void PowerMeter::stepSettled()                  // Once per converged step that changed a pin
{
    integrateVI( Simulator::self()->circTime() );
    m_heldV = m_ePin[0]->getVoltage();
    m_heldI = currentThrough();
    Meter::stepSettled();                     // The base's own integral of the power (its output pin), and the output
}

void PowerMeter::restartAt( uint64_t t )
{
    m_pmBlockStart = m_pmLastT = t;
    m_sumV2 = m_sumI2 = m_sumP = 0;
    m_pmHaveResult = false;
    m_heldV = m_ePin[0]->getVoltage();
    m_heldI = currentThrough();
}

void PowerMeter::closeBlock( double sumV2, double sumI2, double sumP )
{
    double span = (double)m_block;
    m_vRms  = qSqrt( qMax( 0.0, sumV2/span ) );
    m_iRms  = qSqrt( qMax( 0.0, sumI2/span ) );
    m_pMean = sumP/span;
    m_pmHaveResult = true;
}

// As Meter::integrateTo, for the held voltage and current: their squares and their product
void PowerMeter::integrateVI( uint64_t t )
{
    if( t < m_pmLastT ) return;
    uint64_t end = m_pmBlockStart + m_block;
    if( t >= end )
    {
        double dt = (double)( end - m_pmLastT );        // Close the block running now
        double sumV2 = m_sumV2 + m_heldV*m_heldV*dt;
        double sumI2 = m_sumI2 + m_heldI*m_heldI*dt;
        double sumP  = m_sumP  + m_heldV*m_heldI*dt;
        uint64_t whole = ( t - end )/m_block;           // Blocks without a change: the held values
        if( whole ){
            sumV2 = m_heldV*m_heldV*(double)m_block;
            sumI2 = m_heldI*m_heldI*(double)m_block;
            sumP  = m_heldV*m_heldI*(double)m_block;
        }
        closeBlock( sumV2, sumI2, sumP );
        m_sumV2 = m_sumI2 = m_sumP = 0;
        m_pmBlockStart = end + whole*m_block;
        m_pmLastT = m_pmBlockStart;
    }
    double dt = (double)( t - m_pmLastT );
    m_sumV2 += m_heldV*m_heldV*dt;
    m_sumI2 += m_heldI*m_heldI*dt;
    m_sumP  += m_heldV*m_heldI*dt;
    m_pmLastT = t;
}

static void blockValues( double sumV2, double sumI2, double sumP, double span
                       , double& vrms, double& irms, double& pmean )
{
    if( span <= 0 ) return;                  // The caller's held values stand
    vrms  = qSqrt( qMax( 0.0, sumV2/span ) );
    irms  = qSqrt( qMax( 0.0, sumI2/span ) );
    pmean = sumP/span;
}

// As Meter::evalAt: what the block that is running, or the last one, holds at t. Only reads.
void PowerMeter::valuesAt( uint64_t t, double& vrms, double& irms, double& pmean )
{
    vrms = qFabs( m_heldV ); irms = qFabs( m_heldI ); pmean = m_heldV*m_heldI;
    uint64_t end = m_pmBlockStart + m_block;
    if( t >= end + m_block ) return;         // A whole block of the held values
    if( t >= end ){                          // The running block has ended
        double dt = (double)( end - m_pmLastT );
        blockValues( m_sumV2 + m_heldV*m_heldV*dt, m_sumI2 + m_heldI*m_heldI*dt
                   , m_sumP + m_heldV*m_heldI*dt, (double)m_block, vrms, irms, pmean );
        return;
    }
    if( m_pmHaveResult ){ vrms = m_vRms; irms = m_iRms; pmean = m_pMean; return; }
    double dt = (double)( t - m_pmLastT );   // Before the first block ends: so far
    blockValues( m_sumV2 + m_heldV*m_heldV*dt, m_sumI2 + m_heldI*m_heldI*dt
               , m_sumP + m_heldV*m_heldI*dt, (double)( t - m_pmBlockStart ), vrms, irms, pmean );
}

double PowerMeter::voltRms()
{
    double v, i, p;
    valuesAt( Simulator::self()->circTime(), v, i, p );
    return v;
}

double PowerMeter::currRms()
{
    double v, i, p;
    valuesAt( Simulator::self()->circTime(), v, i, p );
    return i;
}

double PowerMeter::meanPower()
{
    double v, i, p;
    valuesAt( Simulator::self()->circTime(), v, i, p );
    return p;
}

double PowerMeter::apparentPower() { return voltRms()*currRms(); }

// Below this r.m.s. voltage or current there is no power factor to give: the lead sits at ground (a few
// hundred nV across the meter's own 1 uOhm), or nothing flows
const double PowerMeter::pfMinV = 1e-6;
const double PowerMeter::pfMinI = 1e-12;

double PowerMeter::powerFactor()
{
    double v = voltRms(), i = currRms();
    if( v < pfMinV || i < pfMinI ) return std::numeric_limits<double>::quiet_NaN();   // The CLI: null
    return qBound( -1.0, meanPower()/(v*i), 1.0 );
}

// "641.3 mW": an SI prefix and four significant digits, as the other meters' valToUnit gives
QString PowerMeter::eng( double v, const QString& unit )
{
    static const char* prefixes[] = { "p", "n", "u", "m", "", "k", "M", "G" };
    double a = qFabs(v);
    if( a < 5e-16 ) return QString( "0.000 %1" ).arg( unit );
    int k = qBound( 0, (int)std::floor( std::log10(a)/3.0 ) + 4, 7 );
    double s = a * std::pow( 10.0, -3.0*(k-4) );
    if( s > 999.95 ){                        // log10(a) fell just short of a power of a thousand
        if( k == 7 ) return QString( "---- %1" ).arg( unit );
        k += 1;
        s = a * std::pow( 10.0, -3.0*(k-4) );
    }
    int decimals = s < 10 ? 3 : s < 100 ? 2 : 1;
    return QString( "%1%2 %3%4" ).arg( v < 0 ? "-" : "" ).arg( QString::number( s, 'f', decimals ) )
                                 .arg( prefixes[k] ).arg( unit );
}

void PowerMeter::updateStep()
{
    Simulator* sim = Simulator::self();
    double p = measured();                   // As the other meters: now, or the block's mean
    double v = 0, i = 0, pm = 0;
    if( sim->isRunning() || sim->isPaused() ) valuesAt( sim->circTime(), v, i, pm );
    else { v = m_ePin[0]->getVoltage(); i = currentThrough(); pm = p; }

    m_dispValue = p;
    QString pf = "----";
    if( v > pfMinV && i > pfMinI ) pf = QString::number( qBound( -1.0, pm/(v*i), 1.0 ), 'f', 3 );
    QString text = eng( p, "W" ) + ( m_reading == rMean ? " avg" : "" )
                 + "\n " + eng( v, "V" )
                 + "\n " + eng( i, "A" )
                 + "\nPF " + pf;
    if( text != m_display.text() ) m_display.setText( text );

    if( m_outLive ) return;                  // Set by the simulation (runEvent)
    m_outPin->setOutHighV( m_dispValue );    // Not wired: only read back (CLI, pin voltage)
    m_outPin->setOutState( true );
}

void PowerMeter::setflip()
{
    Component::setflip();

    int xlabelpos = -40*m_Hflip;             // The display is 80 wide and 52 high
    int ylabelpos = -50*m_Vflip;
    if( m_Vflip < 0 ) ylabelpos -= 48;

    m_display.setPos( xlabelpos, ylabelpos );
    m_display.setTransform( QTransform::fromScale( m_Hflip, m_Vflip ) );
}

void PowerMeter::paint( QPainter* p, const QStyleOptionGraphicsItem* option, QWidget* widget )
{
    Component::paint( p, option, widget );
    p->setBrush( Theme::screenBg() );
    p->drawRoundedRect( QRectF(-42,-52, 84, 60 ), 1, 1 );
    Component::paintSelected( p );
}

void PowerMeter::simState( StateIO& s )
{
    Meter::simState( s );
    s.io( m_heldV );
    s.io( m_heldI );
    s.io( m_sumV2 );
    s.io( m_sumI2 );
    s.io( m_sumP );
    s.io( m_pmBlockStart );
    s.io( m_pmLastT );
    s.io( m_vRms );
    s.io( m_iRms );
    s.io( m_pMean );
    s.io( m_pmHaveResult );
}
