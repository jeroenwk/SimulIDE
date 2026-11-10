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

    protected:
        void updtReactStep();

        virtual void voltSample(){;}   // m_volt has a new value (simulation thread)

        virtual double updtRes(){ return 0.0;}
        // volt: the pin voltage just read; prevVolt: the one at the *previous* update.
        // The inductor's current source accumulates the integral of the voltage, so it
        // must add the sample from the previous boundary, not the one just read: the
        // shunt admit m_admit already carries the current voltage during this interval
        // (BH-053). The capacitor's source is the boundary voltage itself: it takes volt.
        virtual double updtCurr( double volt, double prevVolt )
        { Q_UNUSED(volt); Q_UNUSED(prevVolt); return 0.0;}

        double m_value; // Capacitance or Inductance

        double m_InitCurr;
        double m_curSource;

        double m_InitVolt;
        double m_volt;
        // BH-053: pin voltage to be accumulated at the next update: the one read at the
        // previous update, or the first solved voltage after stamping (m_awaitSolve).
        double m_prevVolt;
        // BH-053: armed at stamp: the next solved voltage is the initial condition.
        bool m_awaitSolve;

        double m_tStep;

        uint64_t m_reacStep;
        uint64_t m_timeStep;

        bool m_running;
};

#endif
