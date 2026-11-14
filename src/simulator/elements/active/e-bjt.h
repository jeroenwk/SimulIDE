/***************************************************************************
 *   Copyright (C) 2018 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EBJT_H
#define EBJT_H

#include "e-element.h"
#include "e-junctioncap.h"

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

        // Junction capacitances B-E and B-C (0 = none; typical 4.5 / 3.6 pF while noise is on)
        double cje() { return m_cje.cj0(); }
        void setCje( double v ) { m_cje.setCj0( v ); }
        double vje() { return m_cje.vj(); }
        void setVje( double v ) { m_cje.setVj( v ); }
        double mje() { return m_cje.mj(); }
        void setMje( double v ) { m_cje.setMj( v ); }
        double cjc() { return m_cjc.cj0(); }
        void setCjc( double v ) { m_cjc.setCj0( v ); }
        double vjc() { return m_cjc.vj(); }
        void setVjc( double v ) { m_cjc.setVj( v ); }
        double mjc() { return m_cjc.mj(); }
        void setMjc( double v ) { m_cjc.setMj( v ); }

        virtual bool pnp()              { return m_PNP; }
        virtual void setPnp( bool pnp ) { m_PNP = pnp; m_changed = true; }

        double threshold() { return m_vCrit; }
        void   setThreshold( double vCrit );

    protected:
        double limitStep( double vnew, double vold );

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
