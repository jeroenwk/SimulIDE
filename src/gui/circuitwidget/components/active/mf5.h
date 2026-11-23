/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// MF5 universal switched-capacitor filter (docs/MF5.md): one section of the MF10.
//   summing op amp: inverting input INV, + at AGND, output N/AP/HP (a high-gain VCVS in the
//                   matrix, like the op amp part)
//   summer:         S = N - S1 - (SA high ? LP : AGND)
//   integrators:    two non-inverting switched-capacitor integrators, S -> BP -> LP, stepped at
//                   each clock rising edge by 2 pi / ratio (ratio 50 or 100 from the 50/100 pin):
//                   f0 = fCLK / ratio, sampled data like the real part
// The external resistors set the mode (MF10 datasheet modes 1 to 6).

#ifndef MF5_H
#define MF5_H

#include "pllvco.h"

class Mf5 : public PllVcoBase
{
    public:
        Mf5( QString type, QString id );
        ~Mf5();

        virtual void simState( StateIO& s ) override;
        SIM_STATE( Mf5 )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void stepSettled() override;

        int ratio() { return m_ratio; }
        double bp() { return m_bp; }
        double lp() { return m_lp; }

        virtual void remove() override;

    private:
        virtual void applyOutputs() override;

        enum { P_N=0, P_BP, P_LP, P_INV, P_S1, P_SA, P_VP, P_VN, P_AGND, P_CLK, P_LSH, P_RATIO };
        enum { E_BP=0, E_LP=2, E_NOUT=4, E_NI, E_NA, E_IBACK, E_ABACK, E_COUNT };

        NortonOut m_bpOut, m_lpOut;

        bool   m_clk;
        int    m_ratio;     // 50, 100, 0 = off
        double m_bp, m_lp;  // Integrator outputs, relative to AGND
        bool   m_stamped;   // Op amp terms in the matrix
};

#endif
