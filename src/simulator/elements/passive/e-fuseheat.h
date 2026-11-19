/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EFUSEHEAT_H
#define EFUSEHEAT_H

// Heat model of a fuse element (specs/fuse.md § 4), on its own: no Qt, no circuit.
// Two stages, in units where the element melts at theta = 1 and the input is u = I^2 (A^2):
//     C1 dθ/dt = u - G1 (θ - φ)          element
//     C2 dφ/dt = G1 (θ - φ) - G2 φ        body -> ambient
// With u constant over an interval the solution is exact (sum of two exponentials), so the
// caller advances it from one simulator solve to the next with no step of its own.
class FuseHeat
{
    public:
        FuseHeat();

        void setParams( double C1, double G1, double C2, double G2 );
        void reset() { m_th = 0; m_ph = 0; }
        void setState( double th, double ph ) { m_th = th; m_ph = ph; }

        double theta() const { return m_th; }
        double phi()   const { return m_ph; }
        double c1()    const { return m_C1; }
        double tau1()  const { return m_C1/m_G1; }          // s

        // u above which the steady state melts: I_mf^2
        double meltU() const { return 1/m_kss; }
        double steadyTheta( double u ) const { return u*m_kss; }

        void   advance( double dt, double u );                // State after dt seconds at u
        double thetaAfter( double dt, double u ) const;       // Same, without changing the state

        // Seconds until θ reaches 1 at constant u from the present state: 0 if already there,
        // -1 if never (u <= meltU: θ stays below max(θ, φ, θss) < 1)
        double timeToMelt( double u ) const { return timeToLevel( u, 1 ); }

        // Seconds until θ crosses `level` at constant u (up if below it, down if above): 0 if
        // already on the far side... -1 if it never gets there (θss on the same side)
        double timeToLevel( double u, double level ) const;

    private:
        void coeffs( double u, double& c0, double& c1 ) const;

        double m_C1, m_G1, m_C2, m_G2;
        double m_kss;                 // θss / u = 1/G1 + 1/G2
        double m_l[2];                // Eigenvalues of the system matrix (< 0)
        double m_v[2][2];             // Eigenvectors in the columns
        double m_vi[2][2];            // Inverse of m_v

        double m_th, m_ph;
};

#endif
