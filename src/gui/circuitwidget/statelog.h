/***************************************************************************
 *   Part states: the current one and every change with its simulated     *
 *   time, written in the simulation thread, read in the GUI thread       *
 ***( see copyright.txt file at root folder )*******************************/

// One state source: a scripted core (component.setStatus) or a built-in part that reports its
// operating state (BJT, MOSFET, SCR...). StatusView draws the current state as a badge, EventLog
// writes the changes to Messages, the CLI's status and read commands list them.
// See docs/PART_STATES.md.

#ifndef STATELOG_H
#define STATELOG_H

#include <QString>
#include <QList>
#include <QMutex>

class StateLog
{
    public:
        struct Event { uint64_t time; QString text; int level; uint64_t seq; };   // seq: unique, rising

        // Simulation thread: a state at simulated time `time`. Only changes are recorded (up to
        // 1000, oldest dropped). Returns true if it was a change
        bool report( QString text, int level, uint64_t time );

        Event current();                  // Thread safe. level -1: nothing reported
        QList<Event> history();           // Changes since the start, oldest first
        void clear();                     // A new run

        // GUI thread, once per frame: the state to draw. A more severe state that came and went
        // since the last frame (shorter than a frame) is held on screen for 3 frames, so it is seen
        Event shown();

        static uint64_t nextSeq();        // Shared by every log: changes of different parts sort in order

    private:
        QMutex m_lock;
        QString m_text;
        int     m_level = -1;
        QList<Event> m_history;

        Event m_peak  = { 0, "", -1, 0 }; // Most severe state since the last shown()
        Event m_held  = { 0, "", -1, 0 };
        int   m_holdFrames = 0;
};

#endif
