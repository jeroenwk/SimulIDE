/***************************************************************************
 *   Event log: part states and capacitor charge events, written to the    *
 *   Messages pane with their simulated time (and kept for the CLI)        *
 ***( see copyright.txt file at root folder )*******************************/

#ifndef EVENTLOG_H
#define EVENTLOG_H

#include <QString>
#include <QStringList>
#include <QHash>
#include <QList>
#include <QPair>
#include <QElapsedTimer>

#include "updatable.h"

class Component;
class CapacitorBase;

class EventLog : public Updatable
{
    public:
        static EventLog* self();

        struct Event {
            uint64_t time;     // Simulated time, ps
            bool     approx;   // Seen at a screen frame (capacitors), not at the exact step
            QString  comp;     // Part label ("IC1")
            QString  uid;
            QString  kind;     // "part" or "cap"
            int      level;    // Component::statusLevel_t
            QString  text;
            bool     shown;    // Written to Messages (false: summarised or over the rate limit)
        };

        bool logParts() { return m_logParts; }
        void setLogParts( bool l );
        bool logCaps() { return m_logCaps; }
        void setLogCaps( bool l );

        QList<Event> events() { return m_events; }    // Since the simulation started

        void attach();      // Simulation started: register, forget the last run
        void stop();        // Simulation stopped: close open cycle summaries

        virtual void updateStep() override;

        static QString timeStr( uint64_t ps );        // "414.817 ms", fixed width

    private:
        EventLog();

        struct Line { uint64_t time; bool approx; int level; QString text; QString sig; };

        struct Track {                      // Per part: cycle summary and rate limit
            QStringList sigs;               // Signatures of the last lines (no numbers)
            QList<uint64_t> times;
            bool cycling = false;
            QStringList pattern;
            int  patIdx  = 0;
            int  repeats = 0;
            uint64_t firstEnd = 0, lastEnd = 0;
            int  lines = 0, dropped = 0;    // In the current wall-clock second
            // Part states
            QHash<QString, QString> last;   // Block -> last text
            uint64_t seq = 0;               // Last state event logged
            // Capacitor
            int  capState = -1;
            bool over = false;
            double peak = 0;
            QList<QPair<uint64_t, double>> samples;
        };

        void partEvents( Component* comp );
        void capEvents( CapacitorBase* cap, uint64_t now );
        void emitLine( Component* comp, QString kind, const Line& line );
        void write( Component* comp, QString kind, const Line& line, bool shown );
        void flushRate();

        bool m_logParts;
        bool m_logCaps;

        QHash<QString, Track> m_tracks;     // By component uid
        QList<Event> m_events;
        QElapsedTimer m_rateTimer;
};

#endif
