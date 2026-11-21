/***************************************************************************
 *   Copyright (C) 2026 by Jeroen de Zwart                                 *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef ECURRREG_H
#define ECURRREG_H

#include <QHash>
#include <QVector>
#include <QPointF>

#include "e-element.h"

// Three-terminal programmable current source (LM334 type), docs/CURRENT_SOURCE_IC.md.
// Pins: 0 V+, 1 V-, 2 R. The set resistor goes from R to V-.
//   v = V+ - V-   (headroom),   u = V_R - V-   (voltage across the set resistor)
// The IC drives F from V+ out of the R pin so that u = Vsense:
//   F = G(v)·(Vsense - u)  (0 when u > Vsense)
// G(v) is the loop transconductance: huge with enough headroom, collapsing below ~0.8 V, which
// gives the turn-on curve (with R_SET, I = Vsense/R · G·R/(1+G·R)). A bias current
// B = F/(n-1) flows from V+ to V-, so I_SET (into V+) = n/(n-1) · Vsense/R. Line regulation is
// one more current V+ to V-, (F+B)·L(v), L = 0 at 2.5 V.
// Vsense is proportional to absolute temperature (PTAT models).

struct currRegData_t{
    double vsense;   // V at 25 °C
    bool   ptat;     // Vsense proportional to T (K)
    double n;        // Typical I_SET/I_BIAS
    QVector<QPointF> ratio; // (log10 I, n) curve; empty: n constant
    double ga, sa;   // log10 G (S) at 0.6 V and slope (decades/V), steep part
    double gb, sb;   // same, shallow part
    double kLo, kHi; // Line regulation (relative change of the current per V), below/above vKnee
    double vKnee;
    double gRev;     // Reverse conductance (S)
    double vMax, vRev, iMax, pMax, vrMax;  // Absolute maximum ratings (states)
};

class eCurrReg : public eElement
{
    public:
        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( eCurrReg )
        eCurrReg( QString id );
        ~eCurrReg();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;

        QString model() { return m_model; }
        void setModel( QString model );

        double vsense() { return m_d.vsense; }   // V at 25 °C
        void setVsense( double v );

        double ratio() { return m_d.n; }
        void setRatio( double n );

        double temp() { return m_temp; }          // °C
        void setTemp( double t ) { m_temp = t; m_changed = true; }

        double iSet()  { return m_iP; }           // Into V+
        double iBias() { return -m_iM; }          // Out of V-
        double iR()    { return -m_iR; }          // Out of R
        double headroom() { return m_v; }
        double vR()    { return m_u; }
        double vsenseNow() { return vsenseAt(); }   // At the junction temperature
        const currRegData_t& modelData() { return m_d; }

 static void getModels();
 static QStringList modelNames();

    protected:
        double vsenseAt();
        double lineAt( double v, double* dl );
        double loopG( double v, double* dg );
        double ratioAt( double i, double* dn );
        void   currents( const double* V, double* I, double J[3][3] );

        currRegData_t m_d;
        QString m_model;
        double  m_temp;

        double m_V0[3];      // Last linearization point
        double m_I0[3];
        double m_J0[3][3];
        double m_v, m_u;     // Last headroom and R voltage (for the GUI)
        double m_iP, m_iM, m_iR;

        eElement m_aux;      // Internal pins, one per ordered pin pair (see stamp())

 static QHash<QString, currRegData_t> m_models;
 static QStringList m_names;
};

#endif
