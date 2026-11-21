/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <QtMath>
#include <QDomDocument>
#include <limits>

#include "e-currreg.h"
#include "e-pin.h"
#include "e-node.h"
#include "simulator.h"
#include "mainwindow.h"
#include "utils.h"

static const double gMin = 1e-12;   // Between every pair of pins: keeps an off part in the matrix
static const double gMax = 100;     // Loop transconductance at full headroom (S): 0.07 % error at 14 Ω

QHash<QString, currRegData_t> eCurrReg::m_models;
QStringList eCurrReg::m_names;

// Internal pin of the ordered pair (k,j), k != j: it stamps g·(V_k - V_j) in row k
static int pairIdx( int k, int j ){ return k*2 + ( j > k ? j-1 : j ); }

eCurrReg::eCurrReg( QString id )
        : eElement( id )
        , m_aux( id+"-aux" )
{
    m_ePin.resize( 3 );
    m_aux.setNumEpins( 6 );

    m_temp  = 25;
    m_model = "Custom";
    // LM334 values (resources/data/currregs.model), so a Custom part works too
    m_d.vsense = 0.064;  m_d.ptat = true;  m_d.n = 18;
    m_d.ga = -3.3; m_d.sa = 17; m_d.gb = -2.7; m_d.sb = 7;
    m_d.kLo = 2e-4; m_d.kHi = 1e-4; m_d.vKnee = 5;
    m_d.gRev = 1.5e-6;
    m_d.vMax = 40; m_d.vRev = 20; m_d.iMax = 10e-3; m_d.pMax = 0.4; m_d.vrMax = 5;

    m_v = m_u = 0;
    m_iP = m_iM = m_iR = 0;
}
eCurrReg::~eCurrReg(){}

void eCurrReg::initialize()
{
    m_changed = true;
    for( int i=0; i<3; ++i ){
        m_V0[i] = 0; m_I0[i] = 0;
        for( int j=0; j<3; ++j ) m_J0[i][j] = 0;
        m_ePin[i]->setTrueCurrent( std::numeric_limits<double>::quiet_NaN() );
    }
    m_v = m_u = 0;
    m_iP = m_iM = m_iR = 0;
}

void eCurrReg::stamp()
{
    for( int i=0; i<6; ++i ) m_aux.getEpin( i )->setEnode( NULL );

    eNode* n[3];
    for( int i=0; i<3; ++i ) n[i] = m_ePin[i]->getEnode();
    if( !n[0] || !n[1] ) return;       // V+ or V- not connected: nothing flows

    // A general 3-terminal Jacobian whose rows sum to zero (currents depend only on voltage
    // differences) is a set of admittances between the pins: one full admittance per ordered pair
    for( int k=0; k<3; ++k )
        for( int j=0; j<3; ++j )
        {
            if( j == k || !n[k] || !n[j] || n[k] == n[j] ) continue;
            ePin* p = m_aux.getEpin( pairIdx( k, j ) );
            p->setEnode( n[k] );
            p->setEnodeComp( n[j] );
        }
    for( int i=0; i<3; ++i ){
        if( !n[i] ) continue;
        n[i]->addToNoLinList( this );
        m_ePin[i]->createCurrent();
    }
}

double eCurrReg::vsenseAt() // At the junction temperature
{
    double tf = m_d.ptat ? ( m_temp+273.15 )/298.15 : 1;
    return m_d.vsense*tf;
}

// Line regulation: relative change of the current with the headroom, zero at the datasheet's
// test point (V+ = 2.5 V). It is an extra current V+ to V- (the finite output impedance), not a
// change of Vsense: a Vsense that moved with the headroom left V_R just above it after a large
// headroom drop (77 uV from 10 to 1.5 V), the part switched off and Newton never settled
double eCurrReg::lineAt( double v, double* dl )
{
    if( v < m_d.vKnee ){ *dl = m_d.kLo; return m_d.kLo*( v-2.5 ); }
    *dl = m_d.kHi;
    return m_d.kLo*( m_d.vKnee-2.5 ) + m_d.kHi*( v-m_d.vKnee );
}

double eCurrReg::loopG( double v, double* dg )
{
    double la = m_d.ga + m_d.sa*( v-0.6 );
    double lb = m_d.gb + m_d.sb*( v-0.6 );
    double sa = m_d.sa, sb = m_d.sb;
    if( la < -20 ){ la = -20; sa = 0; } else if( la > 12 ){ la = 12; sa = 0; }
    if( lb < -20 ){ lb = -20; sb = 0; } else if( lb > 12 ){ lb = 12; sb = 0; }
    double ga = qPow( 10, la );
    double gb = qPow( 10, lb );
    double g = 1/( 1/ga + 1/gb + 1/gMax );
    *dg = g*g*M_LN10*( sa/ga + sb/gb );
    return g;
}

double eCurrReg::ratioAt( double i, double* dn ) // n and dn/di
{
    *dn = 0;
    const QVector<QPointF>& t = m_d.ratio;
    if( t.isEmpty() ) return m_d.n;
    double x = log10( qMax( i, 1e-15 ) );
    if( x <= t.first().x() ) return t.first().y();
    if( x >= t.last().x()  ) return t.last().y();
    int k = 1;
    while( t[k].x() < x ) k++;
    double slope = ( t[k].y()-t[k-1].y() )/( t[k].x()-t[k-1].x() );
    *dn = slope/( i*M_LN10 );
    return t[k-1].y() + slope*( x-t[k-1].x() );
}

// Currents into the part at pin voltages V, and their derivatives J[k][j] = dI_k/dV_j
void eCurrReg::currents( const double* V, double* I, double J[3][3] )
{
    double v = V[0]-V[1];
    double u = V[2]-V[1];
    bool rCon = m_ePin[2]->getEnode() != NULL;

    double F = 0, Fv = 0, Fu = 0;          // Out of R (drawn from V+)
    if( rCon ){
        double dg;
        double g = loopG( v, &dg );
        double e = vsenseAt()-u;
        if( e > 0 ){ F = g*e; Fv = dg*e; Fu = -g; }
    }
    double B = 0, Bf = 0;                  // Bias, V+ to V-
    {
        double dn;
        double n = ratioAt( qMax( F, 1e-12 ), &dn );
        if( n < 1.5 ) n = 1.5;
        B  = F/( n-1 );
        Bf = 1/( n-1 ) - F*dn/( ( n-1 )*( n-1 ) );
    }
    double S = F+B;                        // Set current before line regulation
    double dl;
    double Lr = lineAt( v, &dl );          // Line regulation: X = S·Lr, V+ to V-
    double X = S*Lr;
    double K = 0, Kv = 0;                  // Reverse leakage, V+ to V-
    if( v < 0 ){ K = m_d.gRev*v; Kv = m_d.gRev; }

    double dF[3] = { Fv, -Fv-Fu, Fu };
    double dv[3] = { 1, -1, 0 };
    for( int j=0; j<3; ++j ){
        double dS = ( 1+Bf )*dF[j];
        double dX = dS*Lr + S*dl*dv[j];
        J[0][j] = dS + dX + Kv*dv[j];
        J[1][j] = -Bf*dF[j] - dX - Kv*dv[j];
        J[2][j] = -dF[j];
    }
    I[0] = S + X + K;
    I[1] = -B - X - K;
    I[2] = -F;

    for( int k=0; k<3; ++k )
        for( int j=0; j<3; ++j )
        {
            if( j == k ) continue;
            if( ( k == 2 || j == 2 ) && !rCon ) continue;
            I[k] += gMin*( V[k]-V[j] );
            J[k][k] += gMin;
            J[k][j] -= gMin;
        }
}

void eCurrReg::voltChanged()
{
    if( !m_ePin[0]->getEnode() || !m_ePin[1]->getEnode() ) return;

    double V[3];
    for( int i=0; i<3; ++i ) V[i] = m_ePin[i]->getVoltage();
    if( !m_ePin[2]->getEnode() ) V[2] = V[1];

    double I[3], J[3][3];
    currents( V, I, J );

    m_v = V[0]-V[1];
    m_u = V[2]-V[1];
    m_iP = I[0]; m_iM = I[1]; m_iR = I[2];
    for( int i=0; i<3; ++i ) m_ePin[i]->setTrueCurrent( I[i] );

    if( !m_changed ) // Converged when the last linearization predicts these currents
    {
        double err = 0, mag = 0;
        for( int k=0; k<3; ++k ){
            double pred = m_I0[k];
            for( int j=0; j<3; ++j ) pred += m_J0[k][j]*( V[j]-m_V0[j] );
            err = qMax( err, qFabs( I[k]-pred ) );
            mag = qMax( mag, qFabs( I[k] ) );
        }
        if( err <= 1e-12 + 1e-7*mag ) return;
    }
    m_changed = false;
    Simulator::self()->notCorverged();

    // The turn-on law is exponential in the headroom (0.3..1.3 V): Newton steps go through that
    // band 50 mV at a time, entering it at its edge. Above it G saturates and the linearization
    // does not see the cliff: with a series load the step from 15 V landed below 0 V (off), then
    // back at 15 V, forever. A limited step changes G, so the R node moves and the part is called
    // again even when a source holds V+ and V-
    double v0 = m_V0[0]-m_V0[1];
    double v  = V[0]-V[1];
    double lo = -1e30, hi = 1e30;
    if     ( v0 < 0.3 ) hi = 0.35;
    else if( v0 > 1.3 ) lo = 1.25;
    else { lo = v0-0.05; hi = v0+0.05; }
    if( v < lo || v > hi ){
        V[0] = V[1] + qBound( lo, v, hi );
        currents( V, I, J );
    }
    for( int k=0; k<3; ++k )
    {
        double iEq = -I[k];
        for( int j=0; j<3; ++j ) iEq += J[k][j]*V[j];
        for( int j=0; j<3; ++j ){
            if( j == k ) continue;
            ePin* p = m_aux.getEpin( pairIdx( k, j ) );
            if( p->getEnode() ) p->stampAdmitance( -J[k][j] );
        }
        if( m_ePin[k]->getEnode() ) m_ePin[k]->stampCurrent( iEq );
        m_V0[k] = V[k];
        m_I0[k] = I[k];
        for( int j=0; j<3; ++j ) m_J0[k][j] = J[k][j];
    }
}

static bool differs( double a, double b ){ return qFabs( a-b ) > 1e-4*qMax( qFabs(a), qFabs(b) ); }

void eCurrReg::setVsense( double v )
{
    if( v < 1e-4 ) v = 1e-4;
    if( differs( v, m_d.vsense ) ) m_model = "Custom";
    m_d.vsense = v;
    m_changed = true;
}

void eCurrReg::setRatio( double n ) // A constant ratio replaces the model's curve
{
    if( n < 2 ) n = 2;
    if( !differs( n, m_d.n ) ) return;
    m_model = "Custom";
    m_d.n = n;
    m_d.ratio.clear();
    m_changed = true;
}

void eCurrReg::setModel( QString model )
{
    m_model = model;
    if( !m_models.contains( model ) ) return; // Custom: keep the values
    m_d = m_models.value( model );
    m_changed = true;
}

QStringList eCurrReg::modelNames() { return m_names; }

void eCurrReg::getModels() // Static: resources/data/currregs.model
{
    m_models.clear();
    m_names.clear();

    QString modelsFile = MainWindow::self()->getDataFilePath( "currregs.model" );
    QDomDocument domDoc = fileToDomDoc( modelsFile, "eCurrReg::getModels" );
    QDomNode node = domDoc.documentElement().firstChild();
    while( !node.isNull() )
    {
        QDomElement itemset = node.toElement();
        if( itemset.tagName() == "itemset" && itemset.attribute("type") == "programmable" )
        {
            QDomNode nodei = itemset.firstChild();
            while( !nodei.isNull() )
            {
                QDomElement item = nodei.toElement();
                if( item.tagName() == "item" )
                {
                    auto num = [&]( const char* a, double def ){
                        return item.hasAttribute( a ) ? item.attribute( a ).toDouble() : def; };
                    currRegData_t d;
                    d.vsense = num("vsense", 0.064 );
                    d.ptat   = num("ptat", 1 ) != 0;
                    d.n      = num("n", 18 );
                    for( QString pt : item.attribute("ratio").split(" ", QString::SkipEmptyParts ) ){
                        double i = pt.section(':', 0, 0 ).toDouble();
                        double r = pt.section(':', 1, 1 ).toDouble();
                        if( i > 0 && r > 1 ) d.ratio.append( QPointF( log10( i ), r ) );
                    }
                    d.ga = num("ga",-3.3 ); d.sa = num("sa", 17 );
                    d.gb = num("gb",-2.7 ); d.sb = num("sb", 7 );
                    d.kLo = num("klo", 2e-4 ); d.kHi = num("khi", 1e-4 ); d.vKnee = num("vknee", 5 );
                    d.gRev  = num("grev", 1.5e-6 );
                    d.vMax  = num("vmax", 40 );   d.vRev = num("vrev", 20 );
                    d.iMax  = num("imax", 10e-3 ); d.pMax = num("pmax", 0.4 );
                    d.vrMax = num("vrmax", 5 );
                    QString name = item.attribute("name");
                    if( d.vsense > 0 && d.n > 1 && !name.isEmpty() ){
                        m_models.insert( name, d );
                        m_names.append( name );
                    }
                }
                nodei = nodei.nextSibling();
        }   }
        node = node.nextSibling();
    }
    m_names.append( "Custom" );
}

void eCurrReg::simState( StateIO& s )
{
    eElement::simState( s );
    pinCurrents( s );
    s.io( m_temp );
    s.io( m_V0 );
    s.io( m_I0 );
    s.io( m_v );
    s.io( m_u );
    s.io( m_iP );
    s.io( m_iM );
    s.io( m_iR );
    m_aux.simState( s );
    s.io( m_J0 );
}
