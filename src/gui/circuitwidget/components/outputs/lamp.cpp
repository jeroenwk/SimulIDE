/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QPainter>
#include <QtMath>

#include "lamp.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "pin.h"

#include "doubleprop.h"

#define tr(str) simulideTr("Lamp",str)

Component* Lamp::construct( QString type, QString id )
{ return new Lamp( type, id ); }

LibraryItem* Lamp::libraryItem()
{
    return new LibraryItem(
        tr("Incandescent lamp"),
        "Other Outputs",
        "lamp.png",
        "Lamp",
        Lamp::construct);
}

Lamp::Lamp( QString type, QString id )
    : Comp2Pin( type, id )
    , eResistor( id )
{
    m_area = QRect(-10,-10, 20, 20 );
    m_graphical = true;

    m_maxCurrent = 0.2;
    m_fixedRes   = 5;
    setRes( 5 );

    m_ratedPower = 0;       // Off: a fixed resistance, as before
    m_ratedVolt  = 230;
    m_coldRatio  = 12;      // Tungsten, ~2500-2900 K hot: R_hot / R_cold ~ 12-16 [typical]
    m_thermalTau = 0.04;    // Mains filament of tens of watts: tens of ms [typical]
    m_temp     = 300;
    m_radiated = 0;
    m_thStep   = 20*1e6;    // Filament update every 20 us

    m_ePin[0] = m_pin[0];
    m_ePin[1] = m_pin[1];

    Simulator::self()->addToUpdateList( this );
    Lamp::initialize();

    addPropGroup( { tr("Main"), {
        new DoubProp<Lamp>("Resistance", tr("Resistance"), "Ω"
                          , this, &Lamp::resistance, &Lamp::setResistance ),

        new DoubProp<Lamp>("MaxCurrent", tr("Max Current"), "A"
                          , this, &Lamp::maxCurrent, &Lamp::setMaxCurrent ),
    },0 } );
    addPropGroup( { tr("Filament"), {
        new DoubProp<Lamp>("RatedPower", tr("Rated Power (0 = fixed resistance)"), "W"
                          , this, &Lamp::ratedPower, &Lamp::setRatedPower ),

        new DoubProp<Lamp>("RatedVolt", tr("Rated Voltage"), "V"
                          , this, &Lamp::ratedVolt, &Lamp::setRatedVolt ),

        new DoubProp<Lamp>("ColdRatio", tr("Hot/Cold Resistance"), ""
                          , this, &Lamp::coldRatio, &Lamp::setColdRatio ),

        new DoubProp<Lamp>("ThermalTau", tr("Thermal Time Constant"), "s"
                          , this, &Lamp::thermalTau, &Lamp::setThermalTau ),
    },0 } );
}
Lamp::~Lamp(){}

void Lamp::initialize()
{
    m_crashed = false;
    m_warning = false;
    m_prevStep    = 0;
    m_avgCurrent  = 0.;
    m_intensity   = 25;
    m_brightness  = 0;
    m_totalCurrent = 0;
    m_lastPeriod   = 0;

    m_current = 0;
    m_lastCurrent = 0;

    m_radiated = 0;
    if( thermal() ){            // Cold filament
        setupThermal();
        m_temp  = 300;
        m_admit = 1/m_rCold;
    }
    else m_admit = 1/m_fixedRes;
    update();
}

void Lamp::setupThermal()
{
    const double t0 = 300;
    double rHot = m_ratedVolt*m_ratedVolt/m_ratedPower;
    m_rCold = rHot/m_coldRatio;
    m_tHot  = t0*pow( m_coldRatio, 1/1.2 );                       // R ~ T^1.2
    m_kRad  = m_ratedPower/( pow( m_tHot, 4 )-pow( t0, 4 ) );     // Radiates Prated when hot
    m_cTh   = m_thermalTau*4*m_kRad*pow( m_tHot, 3 );             // tau = C / (dP/dT) at Thot
}

void Lamp::setResistance( double r )
{
    if( r < 1e-6 ) r = 1e-6;
    m_fixedRes = r;
    if( !thermal() ) setResSafe( r );
}

void Lamp::setRatedPower( double p ){ m_ratedPower = p > 0 ? p : 0; if( !thermal() ) setResSafe( m_fixedRes ); }
void Lamp::setRatedVolt( double v ) { if( v > 0 ) m_ratedVolt = v; }
void Lamp::setColdRatio( double r ) { if( r >= 1 ) m_coldRatio = r; }
void Lamp::setThermalTau( double t ){ if( t > 0 ) m_thermalTau = t; }

void Lamp::runEvent()           // Filament temperature, every m_thStep
{
    if( !thermal() ) return;
    const double t0 = 300;
    double volt  = m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage();
    double power = volt*volt*m_admit;
    double dt = m_thStep*1e-12;

    m_radiated = m_kRad*( pow( m_temp, 4 )-pow( t0, 4 ) );
    m_temp += dt*( power-m_radiated )/m_cTh;
    if( m_temp < t0 ) m_temp = t0;
    if( m_temp > 3600 ) m_temp = 3600;                            // Tungsten melts at 3695 K

    double res = m_rCold*pow( m_temp/t0, 1.2 );
    if( qFabs( res*m_admit-1 ) > 1e-4 ) eResistor::setRes( res ); // Re-stamps the admittance

    Simulator::self()->addEvent( m_thStep, this );
}

double Lamp::overCurrent()
{
    if( thermal() ) return sqrt( m_radiated/m_ratedPower );     // Current ratio, hot
    return m_avgCurrent/m_maxCurrent;
}

void Lamp::stamp()
{
    m_ePin[0]->changeCallBack( this );
    m_ePin[1]->changeCallBack( this );

    eResistor::stamp();
    if( thermal() ) Simulator::self()->addEvent( m_thStep, this );
}

void Lamp::updateStep()
{
    uint32_t intensity = m_intensity;
    updateBright();

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
}

void Lamp::voltChanged()
{
    m_current = qFabs( m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage() )*m_admit;
    updateVI();
}

void Lamp::updateVI()
{
    const uint64_t step = Simulator::self()->circTime();
    uint64_t period = (step-m_prevStep);
    m_prevStep = step;
    m_lastPeriod += period;

    if( m_lastCurrent > 0 ) m_totalCurrent += m_lastCurrent*period;
    m_lastCurrent = m_current;
}

void Lamp::updateBright()
{
    if( !Simulator::self()->isRunning() )
    {
        m_totalCurrent = 0;
        m_lastPeriod = 0;
        m_intensity = 25;
        return;
    }
    updateVI();

    uint64_t psPF = Simulator::self()->simPsPF();;

    if( m_lastPeriod > psPF/2 ) // Update 2 times per frame
    {
        m_avgCurrent = m_totalCurrent/m_lastPeriod;
        if( thermal() ){        // Visible light rises steeply with the temperature
            double frac = m_radiated/m_ratedPower;
            m_brightness = frac > 0 ? pow( frac, 1.5 ) : 0;
            if( m_brightness > 1 ) m_brightness = 1;
        }
        else m_brightness = pow( m_avgCurrent/m_maxCurrent, 1.0/2.0 );

        m_totalCurrent  = 0;
        m_lastPeriod = 0;
        m_intensity  = uint32_t(m_brightness*255)+25;
}   }

void Lamp::paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w )
{
    Component::paint( p, o, w );

    QColor color;

    if( m_warning/*m_current > m_maxCurrent*1.2*/ ) // Led overcurrent
    {
        p->setBrush( QColor( 255, 150, 0 ) );
        color = QColor( Qt::red );
    }
    if( m_crashed )  // Led extreme overcurrent
    {
        p->setBrush( Qt::white );
        color = QColor( Qt::white );
    }else{
        if( m_intensity > 25 )
        {
            m_intensity += 15;       // Set a Minimun Bright
            if( m_intensity > 255 ) m_intensity = 255;
            color = QColor( m_intensity, m_intensity, m_intensity*0.85 );
        }
        else color = QColor( 25, 25, 100 );
    }
    p->setBrush( color );
    p->drawEllipse(-8,-8, 16, 16 );

    p->drawLine(-5,-5, 5, 5 );
    p->drawLine(-5, 5, 5,-5 );

    Component::paintSelected( p );
}
