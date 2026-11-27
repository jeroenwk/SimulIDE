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
       , m_vccs( id )
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
    m_vccs.setNumEpins( 5 );
    m_gm = m_gds = m_ieq = 0;
    m_vgs0 = 0;
    m_gmOn = false;
}
eMosfet::~eMosfet(){}

void eMosfet::initialize()
{
    m_step = 0;
    m_gateV = 0;
    m_lastCurrent = 0;
    m_gm = m_gds = m_ieq = 0;
    m_vgs0 = 0;
}

static eNode s_gndNode("");   // Node 0: a term on a node's own column goes to the diagonal

// Drain current into D of the model (it was voltChanged's): square law with the satK slope,
// at most Vds/RDSon; off below the threshold (cero_doub across D-S)
double eMosfet::channel( double vgs, double vds )
{
    double sg = m_Pchannel ? -1 : 1;
    vgs *= sg; vds *= sg;
    double gateV = vgs-m_Gth;
    if( gateV <= 0 ) return sg*cero_doub*vds;
    double admit = 1/m_RDSon;
    double maxCurrDS = vds*admit;
    double satK = 1+vds/100;
    double v = ( vds > gateV ) ? gateV : vds;
    double ds = ( gateV*v-v*v/2 )*satK/m_kRDSon;
    if( ds > maxCurrDS ) ds = maxCurrDS;
    return sg*ds;
}

void eMosfet::stampGm( double gm )
{
    if( !m_gmOn ) return;
    double a[5] = { gm, -gm, -gm, gm, 0 };   // Row D: +gm*Vg -gm*Vs; row S: -gm*Vg +gm*Vs
    for( int i=0; i<5; ++i ){
        ePin* e = m_vccs.getEpin( i );
        if( !e->getEnode() ) continue;
        if( m_gmDiag[i] ) e->stampAdmitance( a[i] );   // Own column: g*V on the diagonal
        else              e->stampSingAdm(-a[i] );     // Other column: -s*V_k
    }
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

    for( int i=0; i<5; ++i ){ m_vccs.getEpin( i )->setEnode( nullptr ); m_gmDiag[i] = false; }
    eNode* nD = m_ePin[0]->getEnode();
    eNode* nS = m_ePin[1]->getEnode();
    eNode* nG = m_ePin[2]->isConnected() ? m_ePin[2]->getEnode() : nullptr;
    m_gmOn = nD && nS && nG;
    if( m_gmOn ){
        eNode* row[5] = { nD, nD, nS, nS, nG };
        eNode* col[5] = { nG, nS, nG, nS, nD };
        for( int i=0; i<5; ++i ){
            if( row[i]->getNodeNumber() == 0 || col[i]->getNodeNumber() == 0 ) continue; // Ground: no row, or V = 0
            ePin* e = m_vccs.getEpin( i );
            e->setEnode( row[i] );
            if( col[i] == row[i] ){ e->setEnodeComp( &s_gndNode ); m_gmDiag[i] = true; }
            else e->addSingAdm( col[i]->getNodeNumber(), 0 );
        }
    }
}

void eMosfet::voltChanged()
{
    double Vd = m_ePin[0]->getVoltage();
    double Vs = m_ePin[1]->getVoltage();
    double Vg = m_ePin[2]->isConnected() ? m_ePin[2]->getVoltage() : Vs;
    double Vgs = Vg-Vs;
    double Vds = Vd-Vs;

    double current = channel( Vgs, Vds );
    double gate = ( m_Pchannel ? -Vgs : Vgs )-m_Gth;
    m_gateV = ( gate > 0 ) ? gate : 0;

    // Converged when the model's current at the solved voltages is the one the stamped
    // linearization gave there (within 1e-4 of itself, at least 1 nA, at most m_accuracy)
    double predicted = m_gds*Vds+( m_gmOn ? m_gm*Vgs : 0 )-m_ieq;
    double tol = qMax( 1e-9, 1e-4*qMax( qFabs( current ), qFabs( predicted ) ) );
    if( qFabs( current-predicted ) < qMin( tol, m_accuracy ) && m_gds > 0 ) return; // Converged
    Simulator::self()->notCorverged();

    // Newton: the slopes of the model (central differences), at a Vgs limited to a step from the
    // last one (as SPICE's fetlim): the model has no gate slope off and at its RDSon cap, so a
    // loop that swung the gate from off to +1000 V landed where gm = 0 and swung back (#426)
    if( m_gmOn ){
        // While the limited point is still off and the gate is on, the stamps at each limited
        // step are the same (gm 0): nothing moves and no node calls it again (a gate stepped far
        // past the threshold, BH-025). Those steps are taken here at once, up to the first one
        // that is on: what calling it again would give, with no re-solve between
        double sg = m_Pchannel ? -1 : 1;
        double vgs = m_vgs0;
        bool onNow = sg*Vgs-m_Gth > 0;
        for( int i=0; i<200; ++i ){
            double l = 1+0.5*qFabs( sg*vgs-m_Gth );
            double d = Vgs-vgs;
            if( d > l ) vgs += l;
            else if( d < -l ) vgs -= l;
            else{ vgs = Vgs; break; }
            if( !onNow || sg*vgs-m_Gth > 0 ) break;   // Off as asked, or on: this step
        }
        Vgs = vgs;
        m_vgs0 = Vgs;
        current = channel( Vgs, Vds );
    }
    double h = 1e-6*( 1+qFabs( Vgs ) );
    double k = 1e-6*( 1+qFabs( Vds ) );
    double gm  = m_gmOn ? ( channel( Vgs+h, Vds )-channel( Vgs-h, Vds ) )/( 2*h ) : 0;
    double gds = ( channel( Vgs, Vds+k )-channel( Vgs, Vds-k ) )/( 2*k );
    if( gds < cero_doub ) gds = cero_doub;

    if( gds != m_admit ){
        eResistor::setAdmit( gds );
        eResistor::stamp();
    }
    if( gm != m_gm ) stampGm( gm );
    m_gm  = gm;
    m_gds = gds;
    m_ieq = gds*Vds+gm*Vgs-current;   // I_D = gds*Vds + gm*Vgs - ieq through this point
    m_lastCurrent = current;
    m_ePin[0]->stampCurrent( m_ieq );     // Into the node: -ieq into the element
    m_ePin[1]->stampCurrent(-m_ieq );
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

void eMosfet::simState( StateIO& s )
{
    eResistor::simState( s );
    s.io( m_accuracy );
    s.io( m_lastCurrent );
    s.io( m_threshold );
    s.io( m_kRDSon );
    s.io( m_RDSon );
    s.io( m_gateV );
    s.io( m_Gth );
    s.io( m_Pchannel );
    s.io( m_depletion );
    s.io( m_gm );
    s.io( m_gds );
    s.io( m_ieq );
    s.io( m_vgs0 );
}
