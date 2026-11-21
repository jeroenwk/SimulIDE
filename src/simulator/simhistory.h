/***************************************************************************
 *   Keyframes of a running simulation (specs/sim-control.md §4.3, §4.5)   *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef SIMHISTORY_H
#define SIMHISTORY_H

#include <cstdint>
#include <deque>
#include <QString>
#include <QStringList>

class Simulator;

// A keyframe each frame, written as raw bytes into one arena allocated once (a ring: when the
// arena is full the oldest keyframes are overwritten). Taking one copies the simulator core,
// every node's stamps and every element's simState(): no allocation, no conversion. The cost
// is measured (capture time per keyframe), as it must stay far below a frame
class SimHistory
{
    public:
 static SimHistory* self();

        bool enabled() { return m_enabled; }
        void setEnabled( bool on );
        uint64_t budget() { return m_budget; }       // Bytes of the arena
        void setBudget( uint64_t bytes );

        // Simulation start: which elements have no state support (empty: history works)
        void start( Simulator* sim );
        void stop();                                 // Keyframes dropped (a stop)
        void end( QString why );                     // History over for this run (an edit): why in lastError()
        bool usable() { return m_enabled && m_usable && !m_ended; }
        QStringList missing() { return m_missing; }  // "Class ×n" without simState

        // GUI timer, worker idle at a solved step
        // replace: a keyframe at the same time is taken again (a change while paused, by the CLI)
        void capture( Simulator* sim, bool replace=false );
        // Load the last keyframe at or before t (worker idle, paused). Returns its time, 0 = none
        // keepClass (negative control of sim history check): elements of that class keep their
        // present state instead of the keyframe's
        uint64_t restore( Simulator* sim, uint64_t t, QString keepClass="" );
        // Drop the keyframes after t (a resume from an earlier time: the old future is gone)
        void truncateAfter( uint64_t t );

        int count() { return (int)m_keys.size(); }
        uint64_t firstTime() { return m_keys.empty() ? 0 : m_keys.front().time; }
        uint64_t lastTime()  { return m_keys.empty() ? 0 : m_keys.back().time; }
        uint64_t bytesUsed();
        uint32_t lastSize()  { return m_keys.empty() ? 0 : m_keys.back().size; }

        // Capture cost
        uint64_t captures()  { return m_captures; }
        double   meanUs()    { return m_captures ? m_totalNs/1e3/m_captures : 0; }
        double   maxUs()     { return m_maxNs/1e3; }
        uint64_t skipped()   { return m_skipped; }   // Frames without a keyframe (step not solved)
        QString  lastError() { return m_error; }
 static QString className( class eElement* el );       // Demangled C++ class

    private:
        SimHistory(){;}
        struct Key { uint64_t time; uint64_t off; uint32_t size; };

        bool     m_enabled = false;
        bool     m_usable  = false;
        bool     m_ended   = false;
        uint64_t m_budget  = 256ull*1024*1024;
        uint8_t* m_arena   = nullptr;
        uint64_t m_arenaSize = 0;
        uint64_t m_head = 0;                 // Where the next keyframe goes
        std::deque<Key> m_keys;              // Oldest first

        QStringList m_missing;
        QString  m_error;
        uint64_t m_captures = 0;
        uint64_t m_skipped  = 0;
        double   m_totalNs  = 0;
        double   m_maxNs    = 0;

        void dropOverlapping( uint64_t from, uint64_t to );
};

#endif
