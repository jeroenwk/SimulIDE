/***************************************************************************
 *   Copyright (C) 2012 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef ERESISTOR_H
#define ERESISTOR_H

#include "e-element.h"

class eResistor : public eElement
{
    public:
        eResistor( QString id );

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( eResistor )
        ~eResistor();

        virtual void stamp() override;
        virtual void noiseSources( QList<NoiseSrc>& list ) override;

        // A physical resistor (thermal noise 4kT/R): set by the resistor-type parts, not by
        // internal resistances of meters, sources, motors... (docs/NOISE_SPEC.md §5.3)
        void setThermalNoise( bool n ) { m_thermalNoise = n; }
        bool thermalNoise() { return m_thermalNoise; }

        virtual double res() { return 1/m_admit; }
        virtual void setRes( double resist );
        double getRes() { return 1/m_admit; }
        virtual void setResSafe( double resist );
        
        double admit() { return m_admit; }
        void setAdmit( double admit );
        void stampAdmit();

        double current();

    protected:
        virtual void updateVI();

        double m_admit;
        double m_current;
        bool   m_thermalNoise = false;
};

#endif
