/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EBJT_H
#define EBJT_H

#include <QHash>
#include <QStringList>

#include "e-element.h"
#include "e-junctioncap.h"

// A part model from resources/data/bjts.model (written by tools/bjt_models.py)
struct bjtData_t
{
    bool   pnp;
    double gain;
    double satCur;  // A
    double va;      // V, 0 = none
    double cje, vje, mje;   // pF, V, -
    double cjc, vjc, mjc;
};

class eBJT : public eElement
{
    public:

        eBJT( QString id );
        virtual ~eBJT();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void noiseSources( QList<NoiseSrc>& list ) override;

        double gain() { return m_gain; }
        void setGain( double gain );

        // Part model: a name from bjts.model sets PNP, gain, IS, VA and the junction capacitances;
        // "Custom" keeps the values. Changing one of them to another value switches to "Custom".
        QString model() { return m_model; }
        void setModel( QString model );
 static void getModels();
 static QStringList modelNames();

        // Junction capacitances B-E and B-C (0 = none; typical 4.5 / 3.6 pF while noise is on)
        double cje() { return m_cje.cj0(); }
        void setCje( double v ) { if( differs( v, cje() ) ) toCustom("Cje"); m_cje.setCj0( v ); }
        double vje() { return m_cje.vj(); }
        void setVje( double v ) { if( differs( v, vje() ) ) toCustom("Vje"); m_cje.setVj( v ); }
        double mje() { return m_cje.mj(); }
        void setMje( double v ) { if( differs( v, mje() ) ) toCustom("Mje"); m_cje.setMj( v ); }
        double cjc() { return m_cjc.cj0(); }
        void setCjc( double v ) { if( differs( v, cjc() ) ) toCustom("Cjc"); m_cjc.setCj0( v ); }
        double vjc() { return m_cjc.vj(); }
        void setVjc( double v ) { if( differs( v, vjc() ) ) toCustom("Vjc"); m_cjc.setVj( v ); }
        double mjc() { return m_cjc.mj(); }
        void setMjc( double v ) { if( differs( v, mjc() ) ) toCustom("Mjc"); m_cjc.setMj( v ); }
        // Typical values used while noise is on (a part built of several BJTs shares a junction)
        void setNoiseCaps( double cje, double cjc ) { m_cje.setNoiseDefault( cje ); m_cjc.setNoiseDefault( cjc ); }

        virtual bool pnp()              { return m_PNP; }
        virtual void setPnp( bool pnp ) { if( pnp != m_PNP ) toCustom("PNP"); m_PNP = pnp; m_changed = true; }

        double threshold() { return m_vCrit; }
        void   setThreshold( double vCrit );

        // Saturation current IS (A): sets VBE (0.596 V at 1 mA for the default 1e-13 A);
        // the same model parameter as the threshold, given the SPICE way
        double satCur() { return m_satCur; }
        void   setSatCur( double is );

        // Early voltage VA (V; 0 = none, infinite output resistance): the transport current
        // is scaled by 1 - VBC/VA (SPICE Gummel-Poon base-width factor), so IC rises with
        // VCE and r_o = (VA + VCE) / IC
        double early() { return m_va; }
        void   setEarly( double va );

        // Collector-emitter breakdown (BVceo, V; 0 = none): avalanche current from C to E
        // (E to C for PNP) once VCE passes it, so a coil switched off with no diode is
        // clamped there instead of jumping to I*L/step
        double bvceo() { return m_bvceo; }
        void   setBvceo( double bv );

        // Emitter-base reverse breakdown (BVebo, V; 0 = none): current from E to B (B to E
        // for PNP) once the base is driven BVebo below the emitter, with the same knee
        double bvebo() { return m_bvebo; }
        void   setBvebo( double bv );

    protected:
        double limitStep( double vnew, double vold );

        // A model value edited: the part is no longer the named model
 static bool differs( double a, double b ){ return qAbs( a-b ) > 1e-4*qMax( qAbs(a), qAbs(b) ); }
        void toCustom( const char* prop );
        QString m_model;
        bool    m_applying;  // setModel() is setting the values: keep the name

 static QHash<QString, bjtData_t> m_bjts;
 static QStringList m_names;

        // Currents into the collector and emitter at the junction voltages (actual VB-VE,
        // VB-VC), and their derivatives when g is given: g = {dIc/dVbe, dIc/dVbc, dIe/dVbe, dIe/dVbc}
        void currents( double vbe, double vbc, double* ic, double* ie, double* g=nullptr );

        double m_va;        // Early voltage, 0 = none

        double m_baseCurr;
        double m_voltBE;
        double m_voltBC;
        double m_gain;
        double m_vt;
        double m_satCur;
        double m_vCrit;
        double m_rgain;
        double m_fgain;
        bool m_PNP;

        double m_gmin;      // Convergence aid in the last stamps
        bool   m_gminDropped;

        double m_bvceo;     // 0 = no breakdown
        double m_voltCE;    // Last linearization point of the breakdown (pnp-signed)
        double m_bdOfset;   // VCE where the breakdown current would be m_satCur
        double m_bdCrit;
        double m_bdCurr;    // Breakdown current into the collector

        double m_bvebo;     // 0 = no E-B breakdown
        double m_voltEB;    // Last linearization point (pnp-signed VEB)
        double m_ebOfset;   // VEB where the breakdown current would be m_satCur
        double m_ebCurr;    // E-B breakdown current into the emitter

        double breakdown( double vce, double* g ) { return breakdown( vce, m_bdOfset, g ); }
        double breakdown( double v, double ofset, double* g );
        double limitBreak( double v, double vold, double ofset );

        eJunctionCap m_cje;
        eJunctionCap m_cjc;

        eElement m_BEjunction;
        eElement m_BCjunction;

        ePin* m_BC;
        ePin* m_CB;
        ePin* m_BE;
        ePin* m_EB;
        ePin* m_CE;
        ePin* m_EC;
};

#endif
