/***************************************************************************
 *   Junction (depletion) capacitance of a semiconductor junction          *
 *   C(V) = Cj0/(1 - V/Vj)^M, the SPICE model (docs/NOISE.md)              *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EJUNCTIONCAP_H
#define EJUNCTIONCAP_H

#include <functional>
#include <utility>

#include "e-reactive.h"

// A capacitor on its own two pins across a junction of a part (anode side first), with
// the capacitance of the junction at its voltage. Off (no stamps at all) while Cj0 is 0,
// unless transient noise is on: then a typical value for the part is used, so loops
// get their real dynamics (docs/NOISE_SPEC.md §15)
class eJunctionCap : public eReactive
{
    public:
        eJunctionCap( QString id );
        ~eJunctionCap();

        // The part's pins the junction sits between, read at each start (P-type parts swap them)
        void setPins( std::function<std::pair<ePin*,ePin*>()> f ) { m_pins = f; }

        double cj0() { return m_cj0; }
        void setCj0( double c ) { m_cj0 = c > 0 ? c : 0; }
        double vj() { return m_vj; }
        void setVj( double v ) { m_vj = v > 0.01 ? v : 0.01; }
        double mj() { return m_m; }
        void setMj( double m ) { m_m = m < 0 ? 0 : ( m > 0.9 ? 0.9 : m ); }

        void setNoiseDefault( double c ) { m_noiseCj0 = c; }   // Used when Cj0 is 0 and noise is on

        double capAt( double v );     // Capacitance at forward voltage v (F)
        double cjUsed() { return m_cjUsed; }   // Cj0 of this run (0: off)
        double capNow() { return m_c; }

        virtual void stamp() override;

    protected:
        virtual void voltSample() override;
        virtual double updtRes()  override { return m_tStep/m_c; }
        virtual double updtCurr() override { return m_volt*m_admit; }
        virtual void rescaleSource( double r ) override { m_curSource *= r; }

        std::function<std::pair<ePin*,ePin*>()> m_pins;

        double m_cj0 = 0;
        double m_vj  = 0.7;
        double m_m   = 0.33;
        double m_noiseCj0 = 0;
        double m_cjUsed = 0;
        double m_c = 1e-12;
};

#endif
