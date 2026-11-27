/***************************************************************************
 *   Copyright (C) 2016 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EMOSFET_H
#define EMOSFET_H

#include "e-resistor.h"
#include "e-junctioncap.h"

class eMosfet : public eResistor
{
    public:
        eMosfet( QString id );
        ~eMosfet();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( eMosfet )

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void noiseSources( QList<NoiseSrc>& list ) override;

        // Gate capacitances G-S and G-D, constant (0 = none; typical 20 / 5 pF while noise is on)
        double cgs() { return m_cgs.cj0(); }
        void setCgs( double c ) { m_cgs.setCj0( c ); }
        double cgd() { return m_cgd.cj0(); }
        void setCgd( double c ) { m_cgd.setCj0( c ); }

        bool pChannel() { return m_Pchannel; }
        virtual void setPchannel( bool p ) { m_Pchannel = p; m_changed = true; }

        bool depletion() { return m_depletion; }
        virtual void setDepletion( bool d ) { m_depletion = d; m_changed = true; }

        double rdson() { return m_RDSon; }
        void setRDSon( double rdson );

        double threshold() { return m_threshold; }
        void setThreshold( double th );
        
    protected:
        void updateValues();
        double channel( double vgs, double vds );   // Drain current (into D) of the model at these
        void stampGm( double gm );                  // The gm terms (VCCS from G-S into D-S)

        double m_accuracy;
        double m_lastCurrent;
        double m_threshold;
        double m_kRDSon;
        double m_RDSon;
        double m_gateV;
        double m_Gth;

        bool m_Pchannel;

        // Newton (create-circuit #426, #529): the channel is linearized at (m_vgs0, m_vds0), with
        // gm from the gate as a VCCS in the matrix (m_vccs ePins "<id>-ePinN": 0, 1 on D for the
        // G and S columns, 2, 3 on S, 4 on G for the solver group). It used to stamp RDSon and a
        // current source with no gate term: a loop through the gate (an op amp regulating the
        // source current) was solved by fixed-point iteration and diverged at loop gain > 1
        eElement m_vccs;
        double m_gm, m_gds, m_ieq;       // Stamped: I_D = m_gds*Vds + m_gm*Vgs - m_ieq
        double m_vgs0;                   // Vgs of the last linearization (limited steps)
        double m_nudge;                  // A limited step: +-1 nA so the solver calls back
        bool   m_gmOn;                    // The gate is connected: the gm terms are stamped
        bool   m_gmDiag[5];

        eJunctionCap m_cgs;
        eJunctionCap m_cgd;
        bool m_depletion;
};

#endif
