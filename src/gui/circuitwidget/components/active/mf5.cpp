/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>

#include "mf5.h"
#include "itemlibrary.h"
#include "simulator.h"
#include "e-resistor.h"
#include "e-node.h"
#include "pin.h"

#define tr(str) simulideTr("Mf5",str)

static eNode s_gnd("");               // Node 0: the op amp output's conductance to ground
static const double A_OL  = 1e5;      // Summing op amp open-loop gain [INFERRED]
static const double G_OUT = 1/10.0;   // Outputs: 10 Ω [INFERRED: sink 1.5 mA, source 3 mA DOC]

Component* Mf5::construct( QString type, QString id )
{ return new Mf5( type, id ); }

LibraryItem* Mf5::libraryItem()
{
    return new LibraryItem(
        tr("SC Filter MF5"),
        "Other Active",
        "ic2.png",
        "MF5",
        Mf5::construct );
}

Mf5::Mf5( QString type, QString id )
   : PllVcoBase( type, id )
{
    m_title = "MF5";
    m_area = QRectF(-24,-26, 48, 52 );
    setLabelPos(-24,-40, 0 );

    m_pin.resize( 12 );
    addPin( P_INV,   "inv",   "INV",     false, 1 );
    addPin( P_S1,    "s1",    "S1",      false, 2 );
    addPin( P_SA,    "sa",    "SA",      false, 3 );
    addPin( P_CLK,   "clk",   "CLK",     false, 4 );
    addPin( P_LSH,   "lsh",   "L.SH",    false, 5 );
    addPin( P_RATIO, "ratio", "50/100",  false, 6 );
    addPin( P_VP,    "vp",    "V+",      true,  1 );
    addPin( P_N,     "n",     "N/AP/HP", true,  2 );
    addPin( P_BP,    "bp",    "BP",      true,  3 );
    addPin( P_LP,    "lp",    "LP",      true,  4 );
    addPin( P_AGND,  "agnd",  "AGND",    true,  5 );
    addPin( P_VN,    "vn",    "V-",      true,  6 );
    for( Pin* p : m_pin ) p->setY( p->y()+4 );    // Rows 1..6: y -16..24 -> -12..28, centred below
    for( Pin* p : m_pin ) p->setY( p->y()-8 );

    setNumEpins( E_COUNT );
    m_bpOut.init( this, id+"-bp", E_BP, m_pin[P_BP] );
    m_lpOut.init( this, id+"-lp", E_LP, m_pin[P_LP] );
}
Mf5::~Mf5(){}

void Mf5::remove()
{
    m_bpOut.remove();
    m_lpOut.remove();
    Component::remove();
}

void Mf5::initialize()
{
    m_pending = false;
    m_clk = false;
    m_ratio = 100;
    m_bp = m_lp = 0;
    m_stamped = false;
}

void Mf5::stamp()
{
    eNode* ag = m_pin[P_AGND]->getEnode();
    m_bpOut.setNodes( ag );
    m_lpOut.setNodes( ag );

    // Summing op amp: G*N - s_inv*V(INV) - s_agnd*V(AGND) = 0, s_inv = -A*G, s_agnd = +A*G
    for( int i : { E_NOUT, E_NI, E_NA, E_IBACK, E_ABACK } ) m_ePin[i]->setEnode( nullptr );
    m_stamped = false;
    eNode* oN  = m_pin[P_N  ]->getEnode();
    eNode* inv = m_pin[P_INV]->getEnode();
    if( oN ){
        m_ePin[E_NOUT]->setEnode( oN );
        m_ePin[E_NOUT]->setEnodeComp( &s_gnd );
        m_ePin[E_NOUT]->stampAdmitance( G_OUT );
        auto term = [&]( int e, int back, eNode* in, double s ){
            if( !in || in->getNodeNumber() == 0 || in == oN ) return;
            m_ePin[e]->setEnode( oN );
            m_ePin[e]->addSingAdm( in->getNodeNumber(), 0 );
            m_ePin[back]->setEnode( in );                      // Zero entry back: same group
            m_ePin[back]->addSingAdm( oN->getNodeNumber(), 0 );
            m_ePin[e]->stampSingAdm( s );
        };
        term( E_NI, E_IBACK, inv, -A_OL*G_OUT );
        term( E_NA, E_ABACK, ag,   A_OL*G_OUT );
        m_stamped = true;
    }
    watch({ P_CLK, P_RATIO, P_SA, P_VP, P_VN, P_AGND });
    later();
}

void Mf5::stepSettled()   // Clock edges: one integration step each
{
    double vp = m_pin[P_VP]->getVoltage();
    double vn = m_pin[P_VN]->getVoltage();
    double ag = m_pin[P_AGND]->isConnected() ? m_pin[P_AGND]->getVoltage() : 0;
    double vr = m_pin[P_RATIO]->isConnected() ? m_pin[P_RATIO]->getVoltage() : ag;
    if     ( vr > ag+0.5*( vp-ag ) ) m_ratio = 50;        // High: 50:1, mid: 100:1, low: off [DOC]
    else if( vr < ag-0.5*( ag-vn ) ) m_ratio = 0;
    else                             m_ratio = 100;

    double th = ( m_pin[P_LSH]->isConnected() ? m_pin[P_LSH]->getVoltage() : vn )+1.5;   // [INFERRED]
    double vc = m_pin[P_CLK]->getVoltage();
    bool clk = m_clk ? vc > th-0.2 : vc > th+0.2;
    bool rise = clk && !m_clk;
    m_clk = clk;
    if( !rise || m_ratio == 0 ) return;

    double n  = m_pin[P_N ]->isConnected() ? m_pin[P_N ]->getVoltage()-ag : 0;
    double s1 = m_pin[P_S1]->isConnected() ? m_pin[P_S1]->getVoltage()-ag : 0;
    bool  saLp = m_pin[P_SA]->isConnected() && m_pin[P_SA]->getVoltage() > ag;
    double sum = n-s1-( saLp ? m_lp : 0 );
    double k = 2*M_PI/m_ratio;
    double hi = vp-ag-1, lo = vn-ag+1;                   // Outputs swing to 1 V of the supplies [DOC]
    m_bp = qBound( lo, m_bp+k*sum,  hi );
    m_lp = qBound( lo, m_lp+k*m_bp, hi );
    later();
}

void Mf5::applyOutputs()
{
    m_bpOut.set( m_bp, G_OUT );
    m_lpOut.set( m_lp, G_OUT );
}

void Mf5::simState( StateIO& s )
{
    PllVcoBase::simState( s );
    m_bpOut.simState( s );
    m_lpOut.simState( s );
    s.io( m_clk );
    s.io( m_ratio );
    s.io( m_bp );
    s.io( m_lp );
    s.io( m_stamped );
}
