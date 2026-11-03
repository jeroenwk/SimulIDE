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
        // Decades of current shown below the largest one (1..6): fewer = more contrast
        static double range() { return m_range; }
        void   setRange( double decades );

        void attach();      // Register with the current Simulator (on every simulation start)
        void clear();       // Remove all dots (simulation stopped)

        // Wire currents, from the element stamps. The simulation worker must be
        // idle: call it from updateStep() or through whenIdle().
        void solve();
        // Run fn after the next solve, while the simulation worker is idle
        void whenIdle( std::function<void()> fn );

        virtual void updateStep() override;

        // Largest wire current right now (A), smoothed: rises at once, decays in about 1 s.
        // Speed and opacity are relative to it, like EveryCircuit: the busiest wire is
        // fastest and brightest, wires carrying 10^-range of it or less crawl, dim.
        double scale() { return m_scale; }

        static double  level( double amps, double threshold, double scale ); // 0..1 (-1 below threshold)
        static double  speed( double amps, double threshold, double scale ); // Dot speed, px/s (0 below threshold)
        static double  opacity( double amps, double threshold, double scale ); // 0.15..1: brighter = more current
        static bool    parseCurrent( QString s, double& amps ); // "1u", "10 nA", "0.5mA", "1e-6"
        static QString currentStr( double amps );               // "1 µA"

        static constexpr double dotSpacing = 16;               // px between dots along a wire


    private:
        CurrentFlow();

        void advance( double dt );
        void updateScale( double dt );

        bool   m_enabled;
        bool   m_shown;          // Dots are on screen
        double m_threshold;
        double m_scale;
        static double m_range;

        QList<std::function<void()>> m_pending;

        QElapsedTimer m_clock;
        qint64 m_lastNs;
};

#endif
