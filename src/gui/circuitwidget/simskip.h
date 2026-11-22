/***************************************************************************
 *   Skip forward by an interval (specs/sim-control.md §3.3–3.4)           *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SIMSKIP_H
#define SIMSKIP_H

#include <QObject>
#include <QTimer>
#include <QElapsedTimer>
#include <functional>

#include "updatable.h"

// One skip at a time: the arrow keys, the toolbar's skip button and the CLI (sim skip, sim step)
// all go through it. A skip runs the simulation as fast as it can (Simulator fast run) up to its
// end, pauses there and waits for one frame, so meters and scopes show the new state.
class SimSkip : public QObject, public Updatable
{
    public:
 static SimSkip* self();

        // Skip count × span (span 0: count solver steps). Starts a stopped simulation (from 0),
        // resumes a paused one. Returns "" when started, else why not (busy, debugger, no event)
        QString start( uint64_t span, int count=1 );
        // Back by count × span (span 0: count solver steps) with the keyframes (§4.3): restore the
        // last one at or before the target, run on to it as fast as it can, pause. "" or why not
        QString startBack( uint64_t span, int count=1 );
        void cancel();                            // Pause where it is
        bool active() { return m_active; }

        // Called once when the current skip ends: ok, and a line for the status bar / CLI
        typedef std::function<void(bool ok, QString msg)> Done;
        void whenDone( Done fn ) { m_done.append( fn ); }

        uint64_t from()   { return m_from; }
        uint64_t target() { return m_target; }

        virtual void updateStep() override { m_frames++; }

        // The skip interval ladder: 1 step (0), then 1 ps … 5 s on 1–2–5, 10 s, 20 s, 30 s, 1 min
 static const QList<uint64_t>& ladder();
        // Next value up (dir +1) or down (-1): factor 0 = one notch, else × or ÷ factor, rounded
        // to the nearest ladder value. Down from 1 ps is 1 step, up from 1 step is 1 ps
 static uint64_t nextInterval( uint64_t ps, int dir, int factor );
 static QString  intervalText( uint64_t ps );               // 0 -> "1 step", 6e13 -> "1 min"
 static bool     parseInterval( QString text, uint64_t* ps ); // "step", "5us", "1 min", "2.5 ms"
 static const uint64_t maxInterval = 60000000000000ull;     // 1 min

    private:
        SimSkip();
        void poll();
        QString backTo( uint64_t target );   // Restore at or before target, then run to it
        void stepBack();                     // One of the steps of a step back
        void finish( bool ok, QString msg );
        void progress();

        bool     m_active = false;
        uint64_t m_from   = 0;
        uint64_t m_target = 0;
        uint64_t m_span   = 0;
        int      m_stepsLeft = 0;    // Solver steps still to do (span 0)
        bool     m_back = false;     // Going back: m_backFrom is where it started
        uint64_t m_backFrom = 0;
        int      m_backSteps = 0;    // Solver steps still to go back
        std::function<void()> m_then; // Next phase when this one ends (internal: no Done call)
        bool     m_announced = false;  // A long skip: its start was logged
        int64_t  m_waitFrame = -1;   // Paused at the end: the frame count to wait past
        int      m_trigCount = 0;    // Triggers::stopCount() at the start: a change = stopped by a trigger
        uint64_t m_frames = 0;
        QTimer   m_poll;
        QElapsedTimer m_wall;
        QList<Done> m_done;
};

#endif
