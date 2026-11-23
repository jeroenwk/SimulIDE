/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "noisesource.h"
#include "theme.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "circuitwidget.h"
#include "e-pin.h"
#include "e-node.h"
#include "pin.h"

#include "doubleprop.h"
#include "intprop.h"

#define tr(str) simulideTr("NoiseSource",str)

Component* NoiseSource::construct( QString type, QString id )
{ return new NoiseSource( type, id ); }

LibraryItem* NoiseSource::libraryItem()
{
    return new LibraryItem(
        tr("Noise Source"),
        "Sources",
        "noisesource.svg",
        "NoiseSource",
        NoiseSource::construct );
}

NoiseSource::NoiseSource( QString type, QString id )
           : Component( type, id )
           , eResistor( id )
{
    m_area = QRectF(-10,-10, 20, 20 );
    m_pin.resize( 2 );
    m_ePin[0] = m_pin[0] = new Pin( 90,  QPoint( 0,-16 ), id+"-plus" , 0, this );
    m_ePin[1] = m_pin[1] = new Pin( 270, QPoint( 0, 16 ), id+"-minus", 1, this );
    m_pin[0]->setLength( 6 );
    m_pin[1]->setLength( 6 );
    setLabelPos( 14,-14, 0 );
    setValLabelPos( 14, 0, 0 );

    m_white = 0.1; m_bw = 10e3;
    m_flicker = 0; m_fmin = 1;
    m_rtsAmp = 0; m_rtsHigh = 1e-3; m_rtsLow = 1e-3;
    m_hum = 0; m_humFreq = 50; m_offset = 0;
    m_outImp = 1;
    m_seedSet = 0;
    m_hold = 1; m_dt = 0; m_volt = 0; m_rtsState = false; m_nf = 0;
    for( int i=0; i<MAXF; ++i ) m_fa[i] = m_fs[i] = m_fx[i] = 0;

    addPropGroup( { tr("Main"), {
        new DoubProp<NoiseSource>("White", tr("White noise (rms)"), "V"
                                 , this, &NoiseSource::white, &NoiseSource::setWhite ),
        new DoubProp<NoiseSource>("Bandwidth", tr("Bandwidth"), "kHz"
                                 , this, &NoiseSource::bw, &NoiseSource::setBw ),
        new DoubProp<NoiseSource>("Offset", tr("DC Offset"), "V"
                                 , this, &NoiseSource::offset, &NoiseSource::setOffset ),
        new DoubProp<NoiseSource>("Out_Imped", tr("Output Impedance"), "Ω"
                                 , this, &NoiseSource::outImp, &NoiseSource::setOutImp ),
        new IntProp<NoiseSource>("Seed", tr("Seed (0 = from the run)"), ""
                                , this, &NoiseSource::seed, &NoiseSource::setSeed, 0, "uint" ),
    },0} );
    addPropGroup( { tr("1/f"), {
        new DoubProp<NoiseSource>("Flicker", tr("1/f density at 1 Hz (V/√Hz)"), "V"
                                 , this, &NoiseSource::flicker, &NoiseSource::setFlicker ),
        new DoubProp<NoiseSource>("Fmin", tr("1/f from"), "Hz"
                                 , this, &NoiseSource::fmin, &NoiseSource::setFmin ),
    },0} );
    addPropGroup( { tr("Telegraph"), {
        new DoubProp<NoiseSource>("RTS_Amp", tr("Telegraph amplitude"), "V"
                                 , this, &NoiseSource::rtsAmp, &NoiseSource::setRtsAmp ),
        new DoubProp<NoiseSource>("RTS_High", tr("Mean time high"), "ms"
                                 , this, &NoiseSource::rtsHigh, &NoiseSource::setRtsHigh ),
        new DoubProp<NoiseSource>("RTS_Low", tr("Mean time low"), "ms"
                                 , this, &NoiseSource::rtsLow, &NoiseSource::setRtsLow ),
    },0} );
    addPropGroup( { tr("Hum"), {
        new DoubProp<NoiseSource>("Hum", tr("Hum amplitude (peak)"), "V"
                                 , this, &NoiseSource::hum, &NoiseSource::setHum ),
        new DoubProp<NoiseSource>("Hum_Freq", tr("Hum frequency"), "Hz"
                                 , this, &NoiseSource::humFreq, &NoiseSource::setHumFreq ),
    },0} );
    setShowProp("White");
}
NoiseSource::~NoiseSource(){}

void NoiseSource::changed()
{
    if( Simulator::self()->isRunning() ) CircuitWidget::self()->powerCircOff();
}

void NoiseSource::setup()
{
    m_hold = (uint64_t)qMax( 1.0, qRound64( 1e12/( 2*m_bw ) )*1.0 );
    m_dt = m_hold*1e-12;

    // 1/f from fmin to the bandwidth: OU processes at corners fc_k, two per decade. Each has a
    // flat density S_k up to fc_k; S_k = c*K/fc_k makes the sum about K/f. The constant c
    // follows from matching the sum to K/f at the middle of the band (in log)
    m_nf = 0;
    if( m_flicker > 0 && m_fmin < m_bw ){
        double K = m_flicker*m_flicker;
        double fc[MAXF];
        for( double f = m_fmin; f < m_bw && m_nf < MAXF; f *= sqrt( 10.0 ) ) fc[m_nf++] = f;
        double fm = sqrt( m_fmin*m_bw );
        double sum = 0;
        for( int k=0; k<m_nf; ++k ) sum += ( 1/fc[k] )/( 1+( fm/fc[k] )*( fm/fc[k] ) );
        double c = ( 1/fm )/sum;
        for( int k=0; k<m_nf; ++k ){
            double Sk  = c*K/fc[k];
            double var = Sk*fc[k]*M_PI/2;                 // ∫ Sk/(1+(f/fc)²) df
            double a   = exp(-2*M_PI*fc[k]*m_dt );
            m_fa[k] = a;
            m_fs[k] = sqrt( var*( 1-a*a ) );
            m_fx[k] = sqrt( var );                         // Start in the stationary spread
        }
    }
}

void NoiseSource::initialize()
{
    uint64_t seed = m_seedSet ? (uint64_t)m_seedSet
                              : ( (uint64_t)Simulator::self()->runSeed()*0x9E3779B97F4A7C15ULL )^qHash( m_elmId );
    m_rng.seed( seed );
    m_nd = std::normal_distribution<double>( 0, 1 );
    m_ud = std::uniform_real_distribution<double>( 0, 1 );
    setup();
    for( int k=0; k<m_nf; ++k ) m_fx[k] *= m_nd( m_rng );
    double pHigh = m_rtsHigh/( m_rtsHigh+m_rtsLow );
    m_rtsState = m_ud( m_rng ) < pHigh;
    m_volt = 0;
}

double NoiseSource::sample()
{
    double t = Simulator::self()->circTime()*1e-12;
    double v = m_offset;
    if( m_white > 0 ) v += m_white*m_nd( m_rng );
    for( int k=0; k<m_nf; ++k ){
        m_fx[k] = m_fa[k]*m_fx[k]+m_fs[k]*m_nd( m_rng );
        v += m_fx[k];
    }
    if( m_rtsAmp != 0 ){
        double tau = m_rtsState ? m_rtsHigh : m_rtsLow;
        if( m_ud( m_rng ) < 1-exp(-m_dt/tau ) ) m_rtsState = !m_rtsState;
        if( m_rtsState ) v += m_rtsAmp;
    }
    if( m_hum != 0 ) v += m_hum*sin( 2*M_PI*m_humFreq*t );
    return v;
}

void NoiseSource::stamp()
{
    if( !m_ePin[0]->isConnected() || !m_ePin[1]->isConnected() ) return;
    m_admit = 1/m_outImp;
    eResistor::stamp();
    m_ePin[0]->createCurrent();
    m_ePin[1]->createCurrent();
    Simulator::self()->addEvent( 1, this );
}

void NoiseSource::runEvent()   // A new value: Norton current G*v into + (out of −)
{
    m_volt = sample();
    double i = m_admit*m_volt;
    m_ePin[0]->stampCurrent( i );
    m_ePin[1]->stampCurrent(-i );
    Simulator::self()->addEvent( m_hold, this );
}

void NoiseSource::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );
    p->setBrush( Theme::body() );
    p->drawEllipse( m_area );
 static const QPointF zz[7] = { QPointF(-6, 1 ), QPointF(-4,-3 ), QPointF(-2, 3 ), QPointF( 0,-4 ),
                                QPointF( 2, 2 ), QPointF( 4,-2 ), QPointF( 6, 1 ) };
    p->drawPolyline( zz, 7 );
    p->drawLine( QPointF(-9,-13 ), QPointF(-5,-13 ) );   // + mark, beside the + pin
    p->drawLine( QPointF(-7,-15 ), QPointF(-7,-11 ) );
    Component::paintSelected( p );
}

void NoiseSource::simState( StateIO& s )
{
    eResistor::simState( s );
    s.io( m_hold );
    s.io( m_dt );
    s.io( m_volt );
    s.io( m_rtsState );
    s.raw( &m_rng, sizeof( m_rng ) );
    s.raw( &m_nd, sizeof( m_nd ) );
    s.raw( &m_ud, sizeof( m_ud ) );
    s.io( m_nf );
    s.io( m_fa );
    s.io( m_fs );
    s.io( m_fx );
}
