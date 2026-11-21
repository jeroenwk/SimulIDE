/***************************************************************************
 *   Junction (depletion) capacitance of a semiconductor junction          *
 ***( see copyright.txt file at root folder )*******************************/

#include <cmath>

#include "e-junctioncap.h"
#include "e-pin.h"
#include "simulator.h"

eJunctionCap::eJunctionCap( QString id )
            : eReactive( id )
{
    setNumEpins( 2 );
}
eJunctionCap::~eJunctionCap()
{
    for( ePin* p : m_ePin ){ p->setEnode( nullptr ); delete p; }
    m_ePin.clear();
}

// SPICE depletion capacitance: Cj0/(1-V/Vj)^M below FC·Vj, linear above (FC = 0.5),
// so it stays finite in forward bias. V is the forward voltage (anode - cathode)
double eJunctionCap::capAt( double v )
{
    const double fc = 0.5;
    if( m_cjUsed <= 0 ) return 0;
    if( m_m == 0 ) return m_cjUsed;
    if( v < fc*m_vj ) return m_cjUsed/std::pow( 1-v/m_vj, m_m );
    return m_cjUsed/std::pow( 1-fc, 1+m_m )*( 1 - fc*(1+m_m) + m_m*v/m_vj );
}

void eJunctionCap::stamp()
{
    m_cjUsed = m_cj0;
    if( m_cjUsed <= 0 && Simulator::self()->noise().on ) m_cjUsed = m_noiseCj0;

    std::pair<ePin*,ePin*> p( nullptr, nullptr );
    if( m_cjUsed > 0 && m_pins ) p = m_pins();
    eNode* a = p.first  ? p.first->getEnode()  : nullptr;
    eNode* b = p.second ? p.second->getEnode() : nullptr;
    if( !a || !b || a == b ){ a = nullptr; b = nullptr; }   // Off: no pins, no stamps
    m_ePin[0]->setEnode( a );
    m_ePin[1]->setEnode( b );

    m_c = m_cjUsed > 0 ? capAt( m_InitVolt ) : 1e-12;
    eReactive::stamp();
}

void eJunctionCap::voltSample()   // New voltage: the capacitance follows it (1 % steps)
{
    if( m_cjUsed <= 0 ) return;
    double c = capAt( m_volt );
    if( !(c > 0) || !std::isfinite( c ) ) return;
    if( std::fabs( c-m_c ) > 0.01*m_c )
    {
        m_c = c;
        eResistor::setRes( updtRes() );
    }
}

void eJunctionCap::simState( StateIO& s )
{
    eReactive::simState( s );
    s.io( m_cj0 );
    s.io( m_vj );
    s.io( m_m );
    s.io( m_noiseCj0 );
    s.io( m_cjUsed );
    s.io( m_c );
}
