/***************************************************************************
 *   Copyright (C) 2026 by the SimulIDE fork contributors                  *
 *                                                                         *
 ***( see copyright.txt file at root folder )*******************************/

// Quartz crystal (docs/CRYSTAL.md): the Butterworth-Van Dyke equivalent circuit, a motional
// branch L1-C1-R1 in series with the shunt capacitance C0 across it:
//
//   pin 0 ──┬── L1 ── C1 ── R1 ──┬── pin 1
//           └──────── C0 ────────┘
//
// One element, integrated with the trapezoidal rule at its own step (Steps per cycle of the
// series resonance): the capacitors and inductors of the library use backward Euler, whose
// numerical damping (about omega*h per radian) would swamp a Q of 10^4..10^5. Trapezoidal
// integration keeps the energy of an LC loop, but shifts its frequency: L1 is pre-warped so
// that the discrete series resonance falls exactly on the set frequency.

#ifndef CRYSTAL_H
#define CRYSTAL_H

#include <QHash>

#include "comp2pin.h"
#include "e-resistor.h"

class LibraryItem;

struct crystalData_t {
    double freq;   // Hz, series resonance
    double c1;     // F, motional capacitance
    double r1;     // Ω, motional resistance
    double c0;     // F, shunt capacitance
};

class Crystal : public Comp2Pin, public eResistor
{
    public:
        Crystal( QString type, QString id );
        ~Crystal();

        virtual void simState( StateIO& s ) override;   // Keyframes (specs/sim-control.md §4.5)
        SIM_STATE( Crystal )

 static Component* construct( QString type, QString id );
 static LibraryItem* libraryItem();

        virtual void initialize() override;
        virtual void stamp() override;
        virtual void runEvent() override;
        virtual void stepSettled() override;

        QString model() { return m_model; }
        void setModel( QString m );

        double freq() { return m_data.freq; }
        void setFreq( double f );
        double c1() { return m_data.c1; }
        void setC1( double c );
        double r1() { return m_data.r1; }
        void setR1( double r );
        double c0() { return m_data.c0; }
        void setC0( double c );

        double steps() { return m_steps; }
        void setSteps( double s );
        double startCurr() { return m_startCurr; }
        void setStartCurr( double i );

        double l1()  { double w = 6.283185307179586*m_data.freq; return 1/( w*w*m_data.c1 ); }
        double q()   { return 6.283185307179586*m_data.freq*l1()/m_data.r1; }
        double fp()  { return m_data.freq*sqrt( 1+m_data.c1/m_data.c0 ); }   // Parallel (anti-) resonance
        double motionalCurrent() { return m_imPrev; }   // A, pin 0 to pin 1 through L1-C1-R1

        virtual void paint( QPainter* p, const QStyleOptionGraphicsItem* o, QWidget* w ) override;

    private:
        void custom();
        void setup();               // Step and companion conductances from the values

        bool    m_setting;
        QString m_model;
        crystalData_t m_data;
        double m_steps;             // Integration steps per cycle of the series resonance
        double m_startCurr;         // Motional current at the start (skips the start-up)

        uint64_t m_hPs;             // Step (ps)
        double m_h;                 // Step (s)
        double m_gm, m_g0;          // Companion conductances: motional branch, C0
        double m_k;                 // 2L/h - R - h/2C1
        double m_ihm, m_ih0;        // Companion currents (history), pin 0 to pin 1
        double m_imPrev;            // Motional current at the last step
        double m_vcm;               // Voltage on C1

 static QHash<QString, crystalData_t> m_models;
};

#endif
