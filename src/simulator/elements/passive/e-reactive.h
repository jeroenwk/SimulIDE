/***************************************************************************
 *   Copyright (C) 2022 by Santiago González                               *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EREACTIVE_H
#define EREACTIVE_H

#include "e-resistor.h"

class eReactive : public eResistor
{
    public:
        eReactive( QString id );
        ~eReactive();

        virtual void stamp() override;
        virtual void voltChanged() override;
        virtual void runEvent() override;

        double initVolt() { return m_InitVolt; }
        void setInitVolt( double v ) { m_InitVolt = v; }

        double initCurr() { return -m_InitCurr; }
        void setInitCurr( double c ) { m_InitCurr = -c; }

        // Transient noise window: integrate with this finer step (ps, 0 = the normal one).
        // Simulation thread
        void setFineStep( uint64_t ps );

    protected:
        void updtReactStep();
        uint64_t baseStep();

        // The step changed: the admittance went from a to a*r. A source that is V·G
        // (capacitor) scales with it; one that holds a current (inductor) does not
        virtual void rescaleSource( double r ){ (void)r; }
        uint64_t m_fineStep = 0;

        virtual void voltSample(){;}   // m_volt has a new value (simulation thread)

        virtual double updtRes(){ return 0.0;}
        virtual double updtCurr(){ return 0.0;}

        double m_value; // Capacitance or Inductance

        double m_InitCurr;
        double m_curSource;

        double m_InitVolt;
        double m_volt;

        double m_tStep;

        uint64_t m_reacStep;
        uint64_t m_timeStep;

        bool m_running;
};

#endif
