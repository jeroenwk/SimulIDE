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

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( eReactive )

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

        // Speed lock (specs/sim-control.md §3.9): integrate with the step × this factor, up to
        // Simulator::lockMaxStep(). Worker idle (GUI timer, after holdWorker); 1 again at stamp
        void setStepScale( int s );

    protected:
        void updtReactStep();
        uint64_t baseStep();

        // The step changed: the admittance went from a to a*r. A source that is V·G
        // (capacitor) scales with it; one that holds a current (inductor) does not
        virtual void rescaleSource( double r ){ (void)r; }
        uint64_t m_fineStep = 0;
        int      m_stepScale = 1;
        void applyStep();          // m_timeStep from baseStep(), the stored current rescaled

        virtual void voltSample(){;}   // m_volt has a new value (simulation thread)

        virtual double updtRes(){ return 0.0;}
        virtual double updtCurr(){ return 0.0;}

        double m_value; // Capacitance or Inductance

        double m_InitCurr;
        double m_curSource = 0;   // Initialized: a part that is off never sets them, and a saved key
                                  // frame held whatever the memory had (specs/sim-control.md §5.1)
        double m_InitVolt;
        double m_volt = 0;

        double m_tStep = 0;

        uint64_t m_reacStep;
        uint64_t m_timeStep = 0;

        bool m_running = false;
};

#endif
