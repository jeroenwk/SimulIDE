/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "crystal.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuitwidget.h"
#include "e-pin.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"
#include "stringprop.h"

// A value read back from its saved text ("400 µA" = 400*1e-6) can be one ulp off the model's
// table value: compare with a relative tolerance so a reopened model doesn't turn Custom.
static inline bool same( double a, double b ) { return qAbs( a-b ) <= 1e-9*qMax( qAbs( a ), qAbs( b ) ); }

#define tr(str) simulideTr("Crystal",str)

// Typical values [INFERRED from vendor tables: HC-49 C1 18-20 fF, C0 <= 7 pF; ESR within the
// maximum of the series; a watch crystal's C1 a few fF, ESR tens of kΩ]: docs/CRYSTAL.md
QHash<QString, crystalData_t> Crystal::m_models = {
    { "32.768 kHz",  { 32768,  2.0e-15, 35e3, 1.5e-12 } },
    { "4 MHz",       { 4e6,    18e-15,  50,   5e-12 } },
    { "10 MHz",      { 10e6,   18e-15,  25,   5e-12 } },
    { "Custom",      { 4e6,    18e-15,  50,   5e-12 } },
};

Component* Crystal::construct( QString type, QString id )
{ return new Crystal( type, id ); }

LibraryItem* Crystal::libraryItem()
{
    return new LibraryItem(
        tr("Crystal"),
        "Reactive",
        "crystal.svg",
        "Crystal",
        Crystal::construct );
}

Crystal::Crystal( QString type, QString id )
       : Comp2Pin( type, id )
       , eResistor( id )
{
    m_area = QRectF(-10,-9, 20, 18 );
    m_ePin[0] = m_pin[0];
    m_ePin[1] = m_pin[1];
    m_setting = false;
    m_steps = 40;
    m_startCurr = 0;
    m_hPs = 0; m_h = 0;
    m_gm = m_g0 = m_k = 0;
    m_ihm = m_ih0 = m_imPrev = m_vcm = 0;
    m_admit = 1e-12;

    addPropGroup( { tr("Main"), {
        new StrProp<Crystal>("Model", tr("Model"), "32.768 kHz,4 MHz,10 MHz,Custom"
                            , this, &Crystal::model, &Crystal::setModel, 0, "enum" ),
        new DoubProp<Crystal>("Frequency", tr("Series Resonance"), "MHz"
                             , this, &Crystal::freq, &Crystal::setFreq ),
        new DoubProp<Crystal>("C1", tr("Motional Capacitance"), "pF"
                             , this, &Crystal::c1, &Crystal::setC1 ),
        new DoubProp<Crystal>("R1", tr("Motional Resistance (ESR)"), "Ω"
                             , this, &Crystal::r1, &Crystal::setR1 ),
        new DoubProp<Crystal>("C0", tr("Shunt Capacitance"), "pF"
                             , this, &Crystal::c0, &Crystal::setC0 ),
    },0} );
    addPropGroup( { tr("Simulation"), {
        new DoubProp<Crystal>("Steps", tr("Steps per Cycle"), ""
                             , this, &Crystal::steps, &Crystal::setSteps ),
        new DoubProp<Crystal>("Start_Current", tr("Motional Current at Start (skips the start-up)"), "mA"
                             , this, &Crystal::startCurr, &Crystal::setStartCurr ),
    },0} );

    m_setting = true;
    setModel("4 MHz");
    m_setting = false;
    setShowProp("Frequency");
}
Crystal::~Crystal(){}

void Crystal::setModel( QString m )
{
    if( !m_models.contains( m ) ) return;
    m_model = m;
    if( m == "Custom" ) return;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
    m_data = m_models.value( m );
    double f = m_data.freq;   // Shown in kHz or MHz (the same value: stays the model)
    setPropStr("Frequency", f >= 1e6 ? QString::number( f/1e6 )+" MHz" : QString::number( f/1e3 )+" kHz");
}

void Crystal::custom()
{
    if( !m_setting ) m_model = "Custom";
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}

void Crystal::setFreq( double f ){ if( f < 1 ) f = 1;         if( same( f, m_data.freq ) ) return; m_data.freq = f; custom(); }
void Crystal::setC1  ( double c ){ if( c < 1e-18 ) c = 1e-18; if( same( c, m_data.c1 ) ) return;   m_data.c1 = c;   custom(); }
void Crystal::setR1  ( double r ){ if( r < 1e-3 ) r = 1e-3;   if( same( r, m_data.r1 ) ) return;   m_data.r1 = r;   custom(); }
void Crystal::setC0  ( double c ){ if( c < 1e-15 ) c = 1e-15; if( same( c, m_data.c0 ) ) return;   m_data.c0 = c;   custom(); }
void Crystal::setSteps( double s )
{
    if( s < 8 ) s = 8;
    m_steps = s;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}
void Crystal::setStartCurr( double i )
{
    m_startCurr = i;
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}

void Crystal::setup()
{
    double f = m_data.freq;

    // The step is reactStep / k (k whole, the smallest one that gives at least Steps per cycle):
    // C0's trapezoidal companion is right only at the crystal's own steps; a solve in between
    // (a capacitor or transistor stepping at reactStep) sees C0's current reversed, and on a
    // high-impedance node the other parts integrate that error every step (a Pierce gate is
    // off by mV, a node with no DC path gets pumped to kV). With the crystal's grid holding
    // every reactStep solve, there is no solve in between (tests/regress/test_crystal_reactstep_grid.py)
    double   want = 1e12/( f*m_steps );                // ps
    uint64_t rs   = Simulator::self()->reactStep();
    if( rs > 0 )
    {
        uint64_t k = (uint64_t)ceil( (double)rs/want - 1e-9 );
        if( k < 1 ) k = 1;
        while( k < rs && rs % k ) k++;                 // A whole number of ps
        m_hPs = rs/k;
    }
    else m_hPs = (uint64_t)qRound64( want );
    if( m_hPs < 1 ) m_hPs = 1;
    m_h = m_hPs*1e-12;

    // Pre-warp: the trapezoidal rule maps the analog frequency wa to wd = (2/h) atan(wa h/2).
    // Choose L1 so the discrete series resonance is the set one: wa = (2/h) tan(w0 h/2)
    double w0 = 6.283185307179586*f;
    double wa = 2/m_h*qTan( w0*m_h/2 );
    double L  = 1/( wa*wa*m_data.c1 );
    double z  = 2*L/m_h + m_data.r1 + m_h/( 2*m_data.c1 );
    m_gm = 1/z;
    m_k  = 2*L/m_h - m_data.r1 - m_h/( 2*m_data.c1 );
    m_g0 = 2*m_data.c0/m_h;
}

void Crystal::initialize()
{
    setup();
    m_imPrev = m_startCurr;                // State at t = 0: motional current, C1 empty, and no
    m_vcm = 0;                             // current from outside (C0 carries -im)
    m_ihm = m_gm*( m_imPrev*m_k );
    m_ih0 = m_startCurr;
}

void Crystal::stamp()
{
    if( !m_ePin[0]->isConnected() || !m_ePin[1]->isConnected() ) return;
    m_admit = m_gm+m_g0;
    eResistor::stamp();
    m_ePin[0]->createCurrent();
    m_ePin[1]->createCurrent();
    double ih = m_ihm+m_ih0;
    m_ePin[0]->stampCurrent(-ih );
    m_ePin[1]->stampCurrent( ih );
    Simulator::self()->addEvent( m_hPs, this );
}

// Each step: stepSettled() reads the voltage solved at the crystal's own step time (other parts'
// events in between would shift it, and that phase error damps a high-Q loop) and computes the
// companion currents for the next interval; the next event stamps them, so the solve at that
// time uses them (stamping from stepSettled() would leave the step unsolved: no keyframes)
void Crystal::runEvent()
{
    double ih = m_ihm+m_ih0;
    m_ePin[0]->stampCurrent(-ih );
    m_ePin[1]->stampCurrent( ih );
    Simulator::self()->addToSettled( this );
    Simulator::self()->addEvent( m_hPs, this );
}

void Crystal::stepSettled()
{
    double v  = m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage();
    double im = m_gm*v+m_ihm;
    double ic = m_g0*v+m_ih0;
    m_vcm += m_h/( 2*m_data.c1 )*( m_imPrev+im );
    m_imPrev = im;
    m_ihm = m_gm*( v+im*m_k-2*m_vcm );
    m_ih0 = -m_g0*v-ic;
}

void Crystal::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    QPen pen = p->pen();
    pen.setWidth( 2 );
    p->setPen( pen );
    p->drawLine(-7,-7,-7, 7 );             // Electrodes
    p->drawLine( 7,-7, 7, 7 );
    pen.setWidthF( 1.5 );
    p->setPen( pen );
    p->setBrush( Theme::body() );
    p->drawRect( QRectF(-4,-9, 8, 18 ) );  // Quartz
    p->drawLine(-11, 0,-7, 0 );
    p->drawLine( 7, 0, 11, 0 );

    Component::paintSelected( p );
}

void Crystal::simState( StateIO& s )
{
    eResistor::simState( s );
    s.io( m_data.freq );
    s.io( m_data.c1 );
    s.io( m_data.r1 );
    s.io( m_data.c0 );
    s.io( m_steps );
    s.io( m_startCurr );
    s.io( m_hPs );
    s.io( m_h );
    s.io( m_gm );
    s.io( m_g0 );
    s.io( m_k );
    s.io( m_ihm );
    s.io( m_ih0 );
    s.io( m_imPrev );
    s.io( m_vcm );
}
