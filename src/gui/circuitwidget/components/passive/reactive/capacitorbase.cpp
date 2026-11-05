/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "capacitorbase.h"
#include "chargeview.h"
#include "simulator.h"
#include "pin.h"

#include "doubleprop.h"
#include "boolprop.h"

#define tr(str) simulideTr("CapacitorBase",str)

CapacitorBase::CapacitorBase( QString type, QString id )
             : Reactive( type, id )
{
    m_area = QRectF(-10,-8, 20, 16 );

    m_pin[0]->setLength( 12 );
    m_pin[1]->setLength( 12 );

    m_value = m_capacitance = 0.00001; // Farads
    m_maxVolt = 0;                           // Rated voltage: 0 = not set
    m_chargeScale = 0;                       // Gauge scale: 0 = the circuit's
    m_logCharge = false;

    addPropGroup( { tr("Main"), {
        new DoubProp<CapacitorBase>("Capacitance", tr("Capacitance"), "µF"
                                   , this, &CapacitorBase::value, &CapacitorBase::setValue ),

        new DoubProp<CapacitorBase>("Resistance" , tr("Resistance"), "µΩ"
                                   , this, &CapacitorBase::resist , &CapacitorBase::setResist ),

        new DoubProp<CapacitorBase>("InitVolt"   , tr("Initial Voltage"), "V"
                                   , this, &CapacitorBase::initVolt, &CapacitorBase::setInitVolt ),

        new DoubProp<CapacitorBase>("MaxVolt"    , tr("Rated Voltage (0 = not set)"), "V"
                                   , this, &CapacitorBase::maxVolt, &CapacitorBase::setMaxVolt ),

        new DoubProp<CapacitorBase>("ChargeScale", tr("Charge gauge 100 % at (0 = circuit)"), "V"
                                   , this, &CapacitorBase::chargeScale, &CapacitorBase::setChargeScale ),

        new BoolProp<CapacitorBase>("LogCharge", tr("Log charge in Messages"), ""
                                   , this, &CapacitorBase::logCharge, &CapacitorBase::setLogCharge ),

        new DoubProp<CapacitorBase>("ReaStep", tr("Reactive Step"), "ns"
                                   , this, &CapacitorBase::reaStep , &CapacitorBase::setReaStep,0 )
    },0 } );

    setShowProp("Capacitance");
    setPropStr( "Capacitance", "10 µF" );

    m_gauge = new ChargeGauge( this, this );
    m_gauge->setVisible( false );
}
CapacitorBase::~CapacitorBase(){}

void CapacitorBase::setCurrentValue( double c )
{
    m_capacitance = c;
    m_changed = true;
}

void CapacitorBase::setMaxVolt( double v )
{
    m_maxVolt = ( v > 0 ) ? v : 0;
    if( chargeShown() ) m_gauge->refresh();     // The gauge draws a snapshot
    update();
}

void CapacitorBase::setChargeScale( double v )
{
    m_chargeScale = ( v > 0 ) ? v : 0;
    if( chargeShown() ) m_gauge->refresh();
    update();
}

void CapacitorBase::snapPlates()
{
    for( int i=0; i<2; ++i ) m_plateVolt[i] = m_pin[i]->isConnected() ? m_pin[i]->getVoltage() : 0;
}

void CapacitorBase::updateStep()
{
    snapPlates();                           // Colours of this frame, also for a capture between frames
    Reactive::updateStep();                 // Repaints the part (plate colours)
    if( chargeShown() ) m_gauge->refresh();
    else if( m_gauge->isVisible() ) m_gauge->setVisible( false );
}

void CapacitorBase::stamp()
{
    m_sampleLock.lock();                  // A new run: samples start at t = 0
    m_samples.clear();
    m_nextSample = 0;
    m_heldVolt = m_InitVolt;
    m_sampleLock.unlock();
    Reactive::stamp();
}

void CapacitorBase::fillTo( uint64_t time )
{
    while( m_nextSample <= time )
    {
        m_samples.append( m_heldVolt );
        m_nextSample += sampleStep;
    }
    const int maxSamples = 1<<15;         // 3.3 s: older points are dropped if nobody takes them
    if( m_samples.size() > maxSamples ) m_samples.remove( 0, m_samples.size()-maxSamples );
}

void CapacitorBase::voltSample()
{
    if( !m_logCharge ) return;
    QMutexLocker lock( &m_sampleLock );
    uint64_t now = Simulator::self()->circTime();
    if( now ) fillTo( now-1 );            // Points before this step keep the old voltage
    m_heldVolt = m_volt;
}

QVector<float> CapacitorBase::takeSamples( uint64_t now, uint64_t* firstTime )
{
    QMutexLocker lock( &m_sampleLock );
    fillTo( now );                        // Nothing changed since the last step: still held
    QVector<float> out;
    out.swap( m_samples );
    *firstTime = m_nextSample - (uint64_t)out.size()*sampleStep;
    return out;
}

bool CapacitorBase::chargeShown()
{
    return ChargeView::self()->active() && m_pin[0]->isConnected() && m_pin[1]->isConnected();
}

void CapacitorBase::chargeDisplayChanged()
{
    snapPlates();
    if( chargeShown() ) m_gauge->refresh();
    else                m_gauge->setVisible( false );
    update();
}

double CapacitorBase::capVolt()
{
    if( !m_pin[0]->isConnected() || !m_pin[1]->isConnected() ) return 0;
    return m_pin[0]->getVoltage() - m_pin[1]->getVoltage();
}

double CapacitorBase::chargeRef()
{
    if( m_chargeScale > 0 ) return m_chargeScale;
    return ChargeView::self()->vref();
}

double CapacitorBase::chargeLevel()
{
    double ref = chargeRef();
    if( ref <= 0 ) return 0;
    return qAbs( capVolt() )/ref;
}

QStringList CapacitorBase::chargeFlags()
{
    QStringList flags;
    double v = capVolt();
    if( m_maxVolt > 0 && qAbs( v ) > m_maxVolt ) flags << "overvoltage";
    if( isReversed() )                         flags << "reversed";
    if( chargeLevel() > 1 )                    flags << "overflow";
    return flags;
}

void CapacitorBase::setPlatePen( QPainter* p, int side )
{
    if( !chargeShown() ) return;
    QPen pen = p->pen();
    pen.setColor( ChargeView::self()->plateColor( m_plateVolt[side], m_chargeScale ) );
    p->setPen( pen );
}

bool CapacitorBase::gaugeWarn()
{
    return ( m_maxVolt > 0 && qAbs( capVolt() ) > m_maxVolt ) || isReversed();
}

double CapacitorBase::gaugeTick()
{
    double ref = chargeRef();
    if( m_maxVolt > 0 && ref > 0 && m_maxVolt < ref ) return m_maxVolt/ref;
    return -1;
}

QString CapacitorBase::gaugeText( int mode )
{
    double v = capVolt();
    QString pct = ChargeView::pctStr( chargeLevel(), qAbs( v ) < 1e-6 );
    if     ( mode == ChargeView::labelVolt    ) return ChargeView::voltStr( v );
    else if( mode == ChargeView::labelPercent ) return pct;
    return ChargeView::voltStr( v )+" · "+pct;
}
