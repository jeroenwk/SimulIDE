/***************************************************************************
 *   Current flow: currents through the wires, shown as moving dots       *
 *   See specs/current-flow.md                                             *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef CURRENTFLOW_H
#define CURRENTFLOW_H

#include <functional>
#include <QList>
#include <QString>
#include <QElapsedTimer>

#include "updatable.h"

class Connector;

class CurrentFlow : public Updatable
{
    public:
        static CurrentFlow* self();

        bool   enabled() { return m_enabled; }
        void   setEnabled( bool en );
        double threshold() { return m_threshold; }  // Smallest current shown (A)
        void   setThreshold( double amps );

        void attach();      // Register with the current Simulator (on every simulation start)
        void clear();       // Remove all dots (simulation stopped)

        // Wire currents, from the element stamps. The simulation worker must be
        // idle: call it from updateStep() or through whenIdle().
        void solve();
        // Run fn after the next solve, while the simulation worker is idle
        void whenIdle( std::function<void()> fn );

        virtual void updateStep() override;

        static double  speed( double amps, double threshold ); // Dot speed, px/s (0 below threshold)
        static double  opacity( double amps, double threshold ); // Dot opacity 0.3..1: brighter = more current
        static bool    parseCurrent( QString s, double& amps ); // "1u", "10 nA", "0.5mA", "1e-6"
        static QString currentStr( double amps );               // "1 µA"

        static constexpr double dotSpacing = 16;               // px between dots along a wire

    private:
        CurrentFlow();

        void advance( double dt );

        bool   m_enabled;
        bool   m_shown;          // Dots are on screen
        double m_threshold;

        QList<std::function<void()>> m_pending;

        QElapsedTimer m_clock;
        qint64 m_lastNs;
};

#endif
