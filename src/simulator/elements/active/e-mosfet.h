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

        double m_accuracy;
        double m_lastCurrent;
        double m_threshold;
        double m_kRDSon;
        double m_RDSon;
        double m_gateV;
        double m_Gth;

        bool m_Pchannel;

        eJunctionCap m_cgs;
        eJunctionCap m_cgd;
        bool m_depletion;
};

#endif
