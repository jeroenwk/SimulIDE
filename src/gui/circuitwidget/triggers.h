/***************************************************************************
 *   Circuit triggers: watch the running simulation, act when a condition  *
 *   is met (specs/triggers.html, specs/sim-control.md §5.2)                *
 ***( see copyright.txt file at root folder )*******************************/

// The circuit owns its triggers (Circuit::triggers()), saved in the .sim1 as <triggers> (D18).
// While a simulation runs they are compiled into a flat list of checks (pin pointers, level,
// hysteresis, armed side). The simulator calls stepDone() on the worker after every solved step;
// a check that fires records a hit and, for a pause trigger, ends the worker's run at that step
// (Simulator::trigStop). updateStep() (GUI thread, worker idle) takes the hits, writes Messages,
// pauses, and disarms single triggers.

#ifndef TRIGGERS_H
#define TRIGGERS_H

#include <QObject>
#include <QList>
#include <QVector>
#include <QString>
#include <QHash>

#include "simulator.h"
#include "updatable.h"
#include "statelog.h"

class Pin;
class Component;

struct TrigCond
{
    QString sig = "volt";   // volt: a pin's voltage (to ground or to a second pin); curr: current into a pin;
                            // state: a part's state, or one block of it (a scripted core of a subcircuit)
    QString comp, pin;      // The watched pin: component uid, pin short name
    QString refComp, ref;   // A pin pair: the second pin (refComp empty: the same part); ref empty: ground
    QString node;           // A node, "uid:pin" of one of its pins (comp / pin then unused)
    QString block;          // state: the block ("" = the part's own state)
    QString from, to;       // state transition: "any" or a state name (case does not matter)
    bool   change = false;  // state: any change
    int    sev = -1;        // state: reaches this severity (Component::stOn … stFault); -1: not used
    QString kind = "edge";  // edge | window | state
    int    edge = 1;        // +1 rising, -1 falling, 0 either
    bool   outside = true;  // window: fires on leaving the band (true) or on entering it
    double level = 0, low = 0, high = 0;
    double hyst = -1;       // < 0: the default (1 % of the level or band, at least 1 mV / 1 µA)

    double hysteresis() const;
    bool operator==( const TrigCond& o ) const;
};

struct Trigger
{
    int  id = 0;
    bool armed = true;
    bool pause = false;     // Action: pause at the step that fired
    bool keyframe = false;  // Action: save a key frame (phase 3b step 2)
    uint64_t before = 0;    // Key frame this long before the hit (ps)
    bool single = true;     // single: disarms itself after firing; else normal
    uint64_t holdoff = 0;   // normal: ignore events this long after a hit (ps)
    int  max = 0;           // normal: disarm after this many hits (0: no limit)
    QString name;
    TrigCond cond;

    // AND (§3, Combining rows): a combined row has members, its conditions; it has the action and
    // mode, the members keep their own (unused while combined, restored by a split). One member
    // is the event (it gives the time), the others are qualifiers, read as states at that step
    QList<Trigger> members;
    bool event = false;     // A member: the AND's event
    bool isAnd() const { return !members.isEmpty(); }

    // Results of this run (not saved)
    QVector<uint64_t> hits;        // Times of the hits
    uint64_t selfDisarmedAt = 0;   // It disarmed itself at this hit (single, max): going back before re-arms
};

class Triggers : public QObject, public SimWatch, public Updatable, public StateWatch
{
    Q_OBJECT
    public:
        Triggers( QObject* parent=nullptr );
        ~Triggers();

 static Triggers* self();   // The current circuit's

        const QList<Trigger>& list() { return m_list; }
        Trigger* find( int id );                     // A row, or a member of an AND
        Trigger* parentOf( int id );                 // The AND a member is in (null: a row)

        // Edits: one undo step each, the circuit marked changed. Apply at the next frame while running
        int  add( Trigger t );                       // Returns its id
        bool set( const Trigger& t );                // Replaces the one with t.id (its hits kept)
        int  remove( QList<int> ids );               // Returns how many were deleted
        int  setArmed( QList<int> ids, bool armed );
        void clearHits( QList<int> ids );

        // AND (§3): rows (or ANDs, merged) combined into one; the first id gives the action and
        // mode; event: a member's id (0: the first one that can be, or the only event-only one)
        QString canCombine( QList<int> ids );        // "" when they can be combined, else why not
        int  combine( QList<int> ids, int event=0, QString name="" );   // The new row's id, 0: refused
        QList<int> split( int id );                  // The members back as rows, disarmed
        bool setEvent( int andId, int memberId );
 static bool eventOnly( const TrigCond& c );        // ↕, any change, the scope: no state to read
        QString qualText( const Trigger& m );        // A member as a qualifier: "is above 10 V"
        void touch() { m_dirty = true; emit changed(); }
        // A part is being deleted (its wires still attached): its own triggers go, node triggers
        // anchored on it move to another pin of the net (D18, §5). Returns the undo XML when
        // something changed ("" otherwise): the caller puts it in the deletion's undo step
        QString partRemoving( Component* comp );
        // Copy / paste (§9): the copied parts' own single-signal triggers (not ANDs), and pasting them
        // onto the new parts (new ids, the same armed state). pasted() returns the undo XML or ""
        QString copyXml( const QStringList& uids );
        QString pasted( const QString& doc, const QHash<QString, QString>& uidMap );
 static QList<Pin*> netPins( Pin* start );         // The pins on the same net (wires and junctions)   // Something a trigger reads changed (the scope's setting)
        int  scopeTrigger( const QString& uid );     // The scope's simulation trigger row (0: none)

        QString check( const Trigger& t );           // "" when every pin it names exists, else why not

        QString watchText( const Trigger& t );       // "R1 pin 1", "Q1 pins G–S", "R5 current (1→2)"
        QString condText( const Trigger& t );        // "↑ 6.32 V", "outside 0 … 1.2 A"
        QString actionText( const Trigger& t );      // "pause", "count only", …
        QString modeText( const Trigger& t );        // "single", "normal · 1 ms · max 10"
        QString describe( const Trigger& t ) { return watchText( t )+" "+condText( t ); }

        // The pause trigger that ended the last run (-1: none yet), its time, and a count that
        // changes at each such pause (sim run --until-trigger, sim run --for, skips)
        int lastStopId() { return m_stopId; }
        uint64_t lastStopTime() { return m_stopTime; }
        int stopCount() { return m_stopCount; }
        QString lastStopText();

        // Saved in the .sim1 (D18)
        QString toXml();
        void fromDoc( const QString& doc );          // The <triggers> element of a circuit file
        void restoreXml( const QString& xml );       // Undo / redo
        void clear();

 static Pin* findPin( const QString& comp, const QString& pin );   // uid + short name (or 1-based number)
 static QString pinName( Pin* p, Component* c );                    // The short name saved in the .sim1
 static Component* comp( const QString& uid );                       // A top-level part (or node) by uid
 static QString pinText( Component* c, Pin* p );                     // "pin 3 VDD", "pin 1", "pin +"
 static QString pinTag( Component* c, Pin* p );                      // "3 VDD", "1", "+" (a pin marker)
 static QList<Pin*> pinsOf( const Trigger& t );                      // The pins it watches (an AND: its members')
 static QString partName( Component* c );                            // Its label, else its uid
 static QStringList statesSeen( Component* c, const QString& block );   // State names reported so far
        QList<int> watchingPart( const QString& uid );               // Triggers on that part's pins or states

        // SimWatch
        virtual void simStarted() override;
        virtual void simStopped() override;
        virtual void stepDone() override;
        virtual void restored() override;
        virtual bool hasState() override { return !m_checks.isEmpty(); }
        virtual void ioState( class StateIO& s ) override;
        // StateWatch
        virtual void stateReport( const void* source, const QString& text, int level, uint64_t time ) override;

 static const void* stateSource( Component* comp, const QString& block );   // StateLog* or ScriptCpu*, null: none
 static QStringList stateBlocks( Component* comp );                         // "" = its own, then the blocks
 static QString levelName( int level );                                     // "OFF", "ON", "WAIT", "FAULT"
 static int levelOf( const QString& name );                                 // -1: not a level
        // Updatable
        virtual void updateStep() override;

    signals:
        void changed();          // Definitions or hits changed (the Triggers tab refreshes)
        void fired( int id );    // A pause trigger ended the run (its row is selected)

    private:
        struct Check
        {
            int id = 0;                                   // The row (an AND's members: the AND's id)
            int key = 0;                                  // Its own id (keyframes, the AND's members)
            bool qual = false;                            // An AND's qualifier: read as a state
            bool qOk = false, qInit = false;              // Its state now
            QVector<int> quals;                           // An AND's event: its qualifiers (indexes)
            bool qualNow( double v );                     // Updates and returns the qualifier's state
            Pin* p0 = nullptr;
            Pin* p1 = nullptr;
            class eNode* n0 = nullptr;   // Their nodes, read directly (both pins have one, or p1 is ground)
            class eNode* n1 = nullptr;
            bool direct = false;
            bool curr = false;
            bool window = false;
            bool state = false;                           // A state condition: the rest below
            const void* src = nullptr;
            QString from, to;
            bool change = false;
            int  sev = -1;
            QString curText;
            int  curLevel = -1;
            struct Ev { QString text; int level; uint64_t time; };
            QVector<Ev> pending;                          // Reports of this step (worker)
            bool fireState( const Ev& e );                // Updates the current state; true: fires
            int  edge = 1;
            bool outside = true;
            double level = 0, lo = 0, hi = 0, h = 0;
            bool armR = false, armF = false, armW = false;   // Armed sides
            class OscopeChannel* scope = nullptr;         // The scope's trigger, Auto level: read each step
            bool needDerive = false;                      // Sides from the value at the next step
            bool pause = false, keyframe = false, single = true;
            uint64_t holdoff = 0, holdEnd = 0;
            int  max = 0, hits = 0;
            bool done = false;                            // Disarmed itself: no more hits

            inline double value();
            bool eval( double v );                        // Updates the armed sides; true: fires
            void derive( double v );                      // Armed sides from the value alone
        };
        struct Hit { int id; uint64_t time; double value; };

        void edited( const QString& undoXml );       // After a change: undo step, recompile
        int  removeIds( const QList<int>& ids );     // remove() without the undo step
        void compile( bool derive );
        bool compileOne( const Trigger& t, Check& c );
        QString condXml( const TrigCond& c );
        QString trigXml( const Trigger& t, bool member );
        Trigger readXml( const class QDomElement& e, const QList<Trigger>& old );
        Trigger asRow( const Trigger& m, const Trigger& from );   // A member as a row with the AND's action and mode
        void summary();
        void keyFrame( Trigger* t, uint64_t hit, bool resumeAfter );   // The key frame action
        bool hit( Check& c, uint64_t t, double v, bool quiet );        // Counts a hit; true: stop there
        struct Saved { bool armR, armF, armW, done; uint64_t holdEnd; int level; QString text; bool qOk; };
        QHash<int, Saved> m_loaded;   // Trigger state read from a keyframe, applied by restored()

        QList<Trigger> m_list;
        int m_nextId = 1;

        Simulator* m_sim = nullptr;   // The run's simulator while running
        QVector<Check> m_checks;      // Worker side: only touched with the worker idle elsewhere
        QVector<Hit>   m_fired;       // Hits of the worker, taken in updateStep()
        bool m_dirty = false;         // Definitions changed: recompile at the next frame
        bool m_firstDone = false;     // The run's first solved step is done (start states don't fire)
        uint64_t m_keepUntil = 0;     // A key frame −Δ in progress: the hits up to the trigger stay
        bool m_preBusy = false;       // A key frame −Δ is being made
        bool m_wasPaused = false;
        QVector<int> m_countNew;      // Count-only hits since the last summary, by trigger id

        int m_stopId = -1;
        uint64_t m_stopTime = 0;
        int m_stopCount = 0;
};

#endif
