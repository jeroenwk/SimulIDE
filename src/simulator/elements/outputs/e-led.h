/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef ELED_H
#define ELED_H

#include "e-resistor.h"
#include "e-junctioncap.h"

class eLed : public eResistor
{
    public:
        eLed( QString id );
        ~eLed();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( eLed )

        virtual void stamp() override;
        virtual void initialize() override;
        virtual void voltChanged() override;
        virtual void noiseSources( QList<NoiseSrc>& list ) override;

        double threshold() { return m_threshold; }
        void  setThreshold( double threshold ) { m_threshold = threshold; m_changed = true; }

        double maxCurrent()             { return m_maxCurrent; }
        void  setMaxCurrent( double c ) { m_maxCurrent = c; m_changed = true; }

        virtual void   setRes( double resist ) override ;
        virtual double res() override { return m_imped; }

        // Junction capacitance C(V) = Cj0/(1-V/Vj)^M (0 = none; typical 20 pF while noise is on)
        double cj0() { return m_cj.cj0(); }
        void setCj0( double v ) { m_cj.setCj0( v ); }
        double vj() { return m_cj.vj(); }
        void setVj( double v ) { m_cj.setVj( v ); }
        double mj() { return m_cj.mj(); }
        void setMj( double v ) { m_cj.setMj( v ); }

        double brightness() { return m_brightness; }
        double overCurrent() { return m_avgCurrent/m_maxCurrent; }

        void updateBright();

    protected:
        virtual void updateVI() override;

        bool m_converged;

        eJunctionCap m_cj;

        uint64_t m_prevStep;
        uint32_t m_intensity;

        double m_maxCurrent;
        double m_avgCurrent;
        double m_totalCurrent;
        double m_lastCurrent;
        double m_lastPeriod;
        double m_brightness;

        double m_voltPN;
        double m_threshold;
        double m_imped;
        double m_lastThCurrent;
};

#endif
