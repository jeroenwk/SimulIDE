/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>
#include <algorithm>

#include "e-fuseheat.h"

FuseHeat::FuseHeat()
{
    m_th = m_ph = 0;
    setParams( 1, 4, 30, 4 );
}

void FuseHeat::setParams( double C1, double G1, double C2, double G2 )
{
    m_C1 = C1; m_G1 = G1; m_C2 = C2; m_G2 = G2;
    m_kss = 1/G1 + 1/G2;

    double a = -G1/C1, b = G1/C1;                // x' = A x + [u/C1, 0]
    double c =  G1/C2, d = -(G1+G2)/C2;
    double tr = a+d, det = a*d-b*c;
    double disc = std::sqrt( std::max( 0.0, tr*tr/4-det ) );
    m_l[0] = tr/2+disc;                          // Slow (closer to 0)
    m_l[1] = tr/2-disc;                          // Fast

    // Eigenvector of λ: (b, λ - a); b > 0, and the two λ differ since c > 0
    m_v[0][0] = b; m_v[0][1] = b;
    m_v[1][0] = m_l[0]-a; m_v[1][1] = m_l[1]-a;

    double dv = m_v[0][0]*m_v[1][1]-m_v[0][1]*m_v[1][0];
    m_vi[0][0] =  m_v[1][1]/dv; m_vi[0][1] = -m_v[0][1]/dv;
    m_vi[1][0] = -m_v[1][0]/dv; m_vi[1][1] =  m_v[0][0]/dv;
}

// θ(t) = θss + c0 e^(λ0 t) + c1 e^(λ1 t): the θ row of V diag(e^λt) V⁻¹ (x0 - xss)
void FuseHeat::coeffs( double u, double& c0, double& c1 ) const
{
    double dth = m_th - u*m_kss;
    double dph = m_ph - u/m_G2;
    c0 = m_v[0][0]*( m_vi[0][0]*dth + m_vi[0][1]*dph );
    c1 = m_v[0][1]*( m_vi[1][0]*dth + m_vi[1][1]*dph );
}

void FuseHeat::advance( double dt, double u )
{
    if( dt <= 0 ) return;
    double dth = m_th - u*m_kss;
    double dph = m_ph - u/m_G2;
    double k0 = ( m_vi[0][0]*dth + m_vi[0][1]*dph )*std::exp( m_l[0]*dt );
    double k1 = ( m_vi[1][0]*dth + m_vi[1][1]*dph )*std::exp( m_l[1]*dt );
    m_th = u*m_kss + m_v[0][0]*k0 + m_v[0][1]*k1;
    m_ph = u/m_G2  + m_v[1][0]*k0 + m_v[1][1]*k1;
}

double FuseHeat::thetaAfter( double dt, double u ) const
{
    if( dt <= 0 ) return m_th;
    double c0, c1;
    coeffs( u, c0, c1 );
    return u*m_kss + c0*std::exp( m_l[0]*dt ) + c1*std::exp( m_l[1]*dt );
}

double FuseHeat::timeToLevel( double u, double level ) const
{
    double thss = u*m_kss;
    bool up = m_th < level;
    if( up  && thss <= level ) return -1;
    if( !up && thss >= level ) return -1;

    double c0, c1;
    coeffs( u, c0, c1 );
    auto beyond = [&]( double t ){
        double th = thss + c0*std::exp( m_l[0]*t ) + c1*std::exp( m_l[1]*t );
        return up ? th >= level : th <= level;
    };
    double lo = 0, hi;
    if( up ){                         // Lower bound: no heat lost, from the hotter of element and body
        hi = ( level-std::max( m_th, m_ph ) )*m_C1/u;
        if( hi < 1e-15 ) hi = 1e-15;
    }
    else hi = 1e-6*tau1();
    while( !beyond( hi ) )            // θ - θss is a sum of two exponentials: one crossing
    {
        lo = hi; hi *= 2;
        if( hi > 1e12 ) return -1;
    }
    for( int i=0; i<200; ++i )        // Bisection down to 1e-12 relative (or 1e-15 s)
    {
        if( hi-lo <= 1e-12*hi || hi-lo < 1e-15 ) break;
        double mid = 0.5*( lo+hi );
        if( beyond( mid ) ) hi = mid;
        else                lo = mid;
    }
    return hi;
}
