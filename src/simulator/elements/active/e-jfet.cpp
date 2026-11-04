/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>
#include <QDomDocument>
#include <limits>

#include "e-jfet.h"
#include "e-pin.h"
#include "e-node.h"
#include "simulator.h"
#include "mainwindow.h"
#include "utils.h"

#define DRAIN  m_ePin[0]
#define SOURCE m_ePin[1]
#define GATE   m_ePin[2]

// Internal pins (m_aux), one per matrix term. Each pin stamps the row of its own node:
// full admittance g·(V_node - V_comp), single admittance -s·V_other.
enum { pDS=0, pDG, pDg, pDs, pSD, pSG, pGS, pGD, pNum };

static const double gMin = 1e-12;   // Keeps cut-off channels and reverse junctions in the matrix

QHash<QString, jfetData_t> eJfet::m_jfets;
QStringList eJfet::m_names;

eJfet::eJfet( QString id )
     : eElement( id )
     , m_aux( id+"-aux" )
{
    m_ePin.resize( 3 );
    m_aux.setNumEpins( pNum );

    m_pChannel = false;
    m_beta   = 1.304e-3;  // 2N3819 (LTspice standard.jft, Vishay)
    m_vto    = -3;
    m_lambda = 2.25e-3;
    m_satCur = 33.57e-15;
    m_emCoef = 1;
    m_model  = "Custom";
    updateVcrit();
}
eJfet::~eJfet(){}

void eJfet::initialize()
{
    m_changed = true;
    m_vgs = 0;
    m_vgd = 0;
    m_vds = 0;
    for( int i=0; i<3; ++i ) m_ePin[i]->setTrueCurrent( std::numeric_limits<double>::quiet_NaN() );
}

void eJfet::stamp()
{
    eNode* dN = DRAIN->getEnode();
    eNode* sN = SOURCE->getEnode();
    eNode* gN = GATE->getEnode();
    if( !dN || !sN ) return;           // Channel not connected: nothing to do

    for( int i=0; i<pNum; ++i ){ m_aux.getEpin( i )->setEnode( NULL ); }

    auto full = [&]( int i, eNode* n, eNode* comp ){
        ePin* p = m_aux.getEpin( i );
        p->setEnode( n );
        p->setEnodeComp( comp );
    };
    auto single = [&]( int i, eNode* n, eNode* other ){
        if( !other || other == n ) return; // Same node: the term is zero
        ePin* p = m_aux.getEpin( i );
        p->setEnode( n );
        p->addSingAdm( other->getNodeNumber(), 0 );
    };
    full( pDS, dN, sN );
    full( pSD, sN, dN );
    if( gN ){                          // Not connected: Vg = Vs, no gate terms
        single( pDs, dN, sN );
        full( pDG, dN, gN );
        full( pSG, sN, gN );
        full( pGS, gN, sN );
        full( pGD, gN, dN );
        single( pDg, dN, gN );
        gN->addToNoLinList( this );
        GATE->createCurrent();
    }
    dN->addToNoLinList( this );
    sN->addToNoLinList( this );
    DRAIN->createCurrent();
    SOURCE->createCurrent();
}

// Square law of the channel for vds >= 0 (N-channel voltages): drain current and its
// derivatives to vgs (gm) and vds (gds)
void eJfet::channel( double vgs, double vds, double* id, double* gm, double* gds )
{
    double vgst = vgs - m_vto;
    if( vgst <= 0 ){ *id = 0; *gm = 0; *gds = 0; return; }   // Cut off

    double k = 1 + m_lambda*vds;
    if( vds < vgst ){                                           // Linear (ohmic)
        *id  = m_beta*vds*(2*vgst - vds)*k;
        *gm  = 2*m_beta*vds*k;
        *gds = m_beta*(2*vgst - 2*vds)*k + m_beta*vds*(2*vgst - vds)*m_lambda;
    }else{                                                      // Saturated (active)
        *id  = m_beta*vgst*vgst*k;
        *gm  = 2*m_beta*vgst*k;
        *gds = m_beta*vgst*vgst*m_lambda;
    }
}

void eJfet::voltChanged()
{
    eNode* dN = DRAIN->getEnode();
    eNode* sN = SOURCE->getEnode();
    if( !dN || !sN ) return;

    double p  = m_pChannel ? -1 : 1;   // Everything below in N-channel voltages, then back
    double vd = DRAIN->getVoltage();
    double vs = SOURCE->getVoltage();
    double vg = GATE->getEnode() ? GATE->getVoltage() : vs;

    double vgs = p*(vg-vs);
    double vgd = p*(vg-vd);

    if( m_changed ) m_changed = false;
    else if( qFabs( vgs-m_vgs ) < 1e-3 && qFabs( vgd-m_vgd ) < 1e-3 ) return; // Converged
    Simulator::self()->notCorverged();

    vgs = limitStep( vgs, m_vgs );     // The gate junctions are exponential: limit the Newton step
    vgd = limitStep( vgd, m_vgd );
    m_vgs = vgs;
    m_vgd = vgd;
    double vds = vgs - vgd;
    m_vds = vds;

    // Channel: drain current (D to S inside) and its derivatives to Vg (a) and Vd (b);
    // the one to Vs is -(a+b). Drain and source swap when vds < 0
    double id, a, b, f, fg, fd;
    if( vds >= 0 ){
        channel( vgs, vds, &f, &fg, &fd );
        id = f; a = fg; b = fd;
    }else{
        channel( vgd,-vds, &f, &fg, &fd );
        id = -f; a = -fg; b = fg+fd;
    }
    b += gMin;
    id += gMin*vds;

    // Gate junctions, gate to source and gate to drain
    double expGS = qExp( vgs/m_vt );
    double expGD = qExp( vgd/m_vt );
    double igs = m_satCur*( expGS-1 ) + gMin*vgs;
    double igd = m_satCur*( expGD-1 ) + gMin*vgd;
    double g1  = m_satCur/m_vt*expGS + gMin;
    double g2  = m_satCur/m_vt*expGD + gMin;

    // Current sources of the linearization (N-channel), then the actual polarity
    double iEqD = id  - a*vgs - b*vds;
    double iEqS = igs - g1*vgs;
    double iEqG = igd - g2*vgd;

    // Rows (current leaving the node into the part = current injected):
    // D: b(Vd-Vs) + a(Vg-Vs) + g2(Vd-Vg) = -iEqD + iEqG
    // S: b(Vs-Vd) + (a+g1)(Vs-Vg)        =  iEqD + iEqS
    // G: g1(Vg-Vs) + g2(Vg-Vd)           = -iEqS - iEqG
    m_aux.getEpin( pDS )->stampAdmitance( b );
    m_aux.getEpin( pSD )->stampAdmitance( b );
    m_aux.getEpin( pDs )->stampSingAdm( a );
    m_aux.getEpin( pDg )->stampSingAdm(-a );
    m_aux.getEpin( pDG )->stampAdmitance( g2 );
    m_aux.getEpin( pSG )->stampAdmitance( a+g1 );
    m_aux.getEpin( pGS )->stampAdmitance( g1 );
    m_aux.getEpin( pGD )->stampAdmitance( g2 );

    DRAIN ->stampCurrent( p*(-iEqD + iEqG) );
    SOURCE->stampCurrent( p*( iEqD + iEqS) );
    GATE  ->stampCurrent( p*(-iEqS - iEqG) );

    // True terminal currents (into the part): the stamps are a linearization
    DRAIN ->setTrueCurrent( p*( id - igd ) );
    SOURCE->setTrueCurrent( p*(-id - igs ) );
    GATE  ->setTrueCurrent( p*( igs + igd ) );
}

double eJfet::limitStep( double vnew, double vold ) // As the BJT: pnjlim of SPICE
{
    if( vnew > m_vCrit && qFabs( vnew-vold ) > 2*m_vt ){
        if( vold > 0 ){
            double arg = 1+(vnew-vold)/m_vt;
            if( arg > 0 ) vnew = vold + m_vt*qLn( arg );
            else          vnew = m_vCrit;
        }
        else vnew = m_vt*qLn( vnew/m_vt );
    }
    return vnew;
}

void eJfet::updateVcrit()
{
    m_vt = 0.025865*m_emCoef;
    m_vCrit = m_vt*qLn( m_vt/( qSqrt(2)*m_satCur ) );
    m_changed = true;
}

static bool differs( double a, double b ){ return qFabs( a-b ) > 1e-4*qMax( qFabs(a), qFabs(b) ); }

void eJfet::setIdss( double idss )
{
    if( idss < 1e-9 ) idss = 1e-9;
    if( differs( idss, this->idss() ) ) m_model = "Custom";
    m_beta = idss/( m_vto*m_vto );
    m_changed = true;
}

void eJfet::setVto( double vto ) // Pinch-off voltage, negative (SPICE sign, also for P channel); keeps IDSS
{
    if( vto > 0 ) vto = -vto;      // Pinch-off given with the other sign
    if( vto > -0.01 ) vto = -0.01;
    if( differs( vto, m_vto ) ) m_model = "Custom";
    double idss = this->idss();
    m_vto  = vto;
    m_beta = idss/( vto*vto );
    m_changed = true;
}

void eJfet::setLambda( double l )
{
    if( l < 0 ) l = 0;
    if( differs( l, m_lambda ) ) m_model = "Custom";
    m_lambda = l;
    m_changed = true;
}

void eJfet::setSatCur( double is )
{
    if( is < 1e-20 ) is = 1e-20;
    if( differs( is, m_satCur ) ) m_model = "Custom";
    m_satCur = is;
    updateVcrit();
}

void eJfet::setEmCoef( double n )
{
    if( n < 0.5 ) n = 0.5;
    if( differs( n, m_emCoef ) ) m_model = "Custom";
    m_emCoef = n;
    updateVcrit();
}

void eJfet::setModel( QString model )
{
    m_model = model;
    if( !m_jfets.contains( model ) ) return; // Custom: keep the values
    jfetData_t d = m_jfets.value( model );
    m_pChannel = d.pChannel;
    m_beta   = d.beta;
    m_vto    = d.vto;
    m_lambda = d.lambda;
    m_satCur = d.satCur;
    m_emCoef = d.emCoef;
    updateVcrit();
}

QStringList eJfet::modelNames() { return m_names; }

void eJfet::getModels() // Static: resources/data/jfets.model
{
    m_jfets.clear();
    m_names.clear();

    QString modelsFile = MainWindow::self()->getDataFilePath( "jfets.model" );
    QDomDocument domDoc = fileToDomDoc( modelsFile, "eJfet::getModels" );
    QDomNode node = domDoc.documentElement().firstChild();
    while( !node.isNull() )
    {
        QDomElement itemset = node.toElement();
        if( itemset.tagName() == "itemset" )
        {
            bool pCh = itemset.attribute("type").toUpper() == "PJF";
            QDomNode nodei = itemset.firstChild();
            while( !nodei.isNull() )
            {
                QDomElement item = nodei.toElement();
                if( item.tagName() == "item" )
                {
                    jfetData_t d;
                    d.pChannel = pCh;
                    d.beta   = item.attribute("beta").toDouble();
                    d.vto    = item.attribute("vto").toDouble();
                    d.lambda = item.attribute("lambda").toDouble();
                    d.satCur = item.attribute("is").toDouble();
                    d.emCoef = item.hasAttribute("n") ? item.attribute("n").toDouble() : 1;
                    QString name = item.attribute("name");
                    if( d.beta > 0 && d.vto < 0 && d.satCur > 0 ){
                        m_jfets.insert( name, d );
                        m_names.append( name );
                    }
                }
                nodei = nodei.nextSibling();
        }   }
        node = node.nextSibling();
    }
    m_names.append( "Custom" );
}
