/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>   // qFabs(x,y)
#include <QDebug>

#include "e-mosfet.h"
#include "noise.h"
#include "simulator.h"
#include "e-pin.h"
#include "e-node.h"

eMosfet::eMosfet( QString id )
       : eResistor( id )
       , m_cgs( id+"-Cgs" )
       , m_cgd( id+"-Cgd" )
{
    m_cgs.setPins( [this](){ return std::make_pair( m_ePin[2], m_ePin[1] ); } );
    m_cgd.setPins( [this](){ return std::make_pair( m_ePin[2], m_ePin[0] ); } );
    m_cgs.setMj( 0 );   // Oxide capacitance: constant
    m_cgd.setMj( 0 );
    m_cgs.setNoiseDefault( 20e-12 );
    m_cgd.setNoiseDefault( 5e-12 );
    m_Pchannel  = false;
    m_depletion = false;
    
    m_gateV     = 0;
    m_RDSon     = 1;
    m_threshold = 3;

    m_ePin.resize(3);
}
eMosfet::~eMosfet(){}

void eMosfet::initialize()
{
    m_step = 0;
    m_gateV = 0;
    m_lastCurrent = 0;
}

void eMosfet::stamp()
{
    m_admit = 1/m_RDSon;

    updateValues();
    m_accuracy = 5e-6;

    if( (m_ePin[0]->isConnected())
      &&(m_ePin[1]->isConnected()) )
    {
        eResistor::stamp();

        m_ePin[0]->createCurrent();
        m_ePin[1]->createCurrent();

        m_ePin[0]->getEnode()->addToNoLinList(this);
        m_ePin[1]->getEnode()->addToNoLinList(this);
    }
    if( m_ePin[2]->isConnected() ) m_ePin[2]->getEnode()->addToNoLinList(this);
}

void eMosfet::voltChanged()
{
    double Vd = m_ePin[0]->getVoltage();
    double Vs = m_ePin[1]->getVoltage();
    double Vg = m_ePin[2]->isConnected() ? m_ePin[2]->getVoltage() : Vs;
    double Vgs = Vg-Vs;
    double Vds = Vd-Vs;

    if( m_Pchannel ){ Vgs = -Vgs; Vds = -Vds; }
    
    double gateV = Vgs - m_Gth;
    
    if( gateV < 0 ) gateV = 0;

    double admit = cero_doub;
    double current = 0;
    
    if( gateV > 0 )
    {
        admit = 1/m_RDSon;
        double maxCurrDS = Vds*admit;
        double satK = 1+Vds/100;

        if( Vds > gateV ) Vds =gateV;

        double DScurrent = (gateV*Vds-qPow(Vds,2)/2)*satK/m_kRDSon;
        if( DScurrent > maxCurrDS ) DScurrent = maxCurrDS;
        current = maxCurrDS-DScurrent;
    }
    if( m_Pchannel ) current = -current;
    
    if( admit != m_admit ){
        eResistor::setAdmit( admit );
        eResistor::stamp();
    }
    m_gateV = gateV;

    // Converged when the current changes by less than 1e-4 of itself (at least 1 nA). A fixed
    // 5 uA was 1 % of a 0.4 mA small-signal amplifier: its output came out in steps
    double tol = qMax( 1e-9, 1e-4*qMax( qFabs(current), qFabs(m_lastCurrent) ) );
    if( qFabs(current-m_lastCurrent) < qMin( tol, m_accuracy ) ) return; // Converged
    Simulator::self()->notCorverged();

    m_lastCurrent = current;
    m_ePin[0]->stampCurrent( current );
    m_ePin[1]->stampCurrent(-current );
}

void eMosfet::noiseSources( QList<NoiseSrc>& list )   // Channel thermal noise 4kT·(2/3)·gm
{
    if( !m_ePin[0] || !m_ePin[1] ) return;
    NoiseSrc s;
    s.a = m_ePin[0]; s.b = m_ePin[1];
    s.kind = NoiseSrc::Channel;
    s.psd = [this]()
    {
        if( m_gateV <= 0 || m_kRDSon <= 0 ) return 0.0;      // Cut off
        double vds = qFabs( m_ePin[0]->getVoltage()-m_ePin[1]->getVoltage() );
        double gm = qMin( vds, m_gateV )*( 1+vds/100 )/m_kRDSon; // d(DScurrent)/d(gateV) of the model
        return Noise::fourKT*2.0/3.0*gm;
    };
    list.append( s );
}

void eMosfet::setRDSon( double rdson )
{
    if( rdson < cero_doub ) rdson = cero_doub;
    if( rdson > 1000 ) rdson = 1000;
    m_RDSon = rdson;
    m_changed = true;
}

// A negative Threshold is a depletion part's pinch-off, as a datasheet gives it (LND150:
// -1..-3 V): the channel turns off at V_GS = Threshold exactly. It used to be refused
// silently, so a depletion part had to be tuned through Depletion=true and a positive
// Threshold T, which turns off at -1.25 T (create-circuit #425). Kept for old circuits.
void eMosfet::setThreshold( double th )
{
    if( th >= 0 && th < 0.01 ) return;
    if( th < -1000 ) th = -1000;
    m_threshold = th; 
    updateValues();
    m_changed = true;
}

void eMosfet::updateValues()
{
    // The channel constant was RDSon*(10-Threshold): zero or negative for Threshold >= 10, which
    // inverted the channel current and put the drain far above the supply (BH-025). At least 1.
    double span = 10-m_threshold;
    if( span < 1 ) span = 1;
    m_kRDSon = m_RDSon*span;
    if( m_threshold < 0 ) m_Gth = m_threshold;               // pinch-off as given (#425)
    else m_Gth = m_depletion ? -m_threshold-m_threshold/4 : m_threshold-m_threshold/4;
    qDebug() << "[eMosfet] updateValues Threshold" << m_threshold << "depletion" << m_depletion
             << "Gth" << m_Gth << "kRDSon" << m_kRDSon;
}
