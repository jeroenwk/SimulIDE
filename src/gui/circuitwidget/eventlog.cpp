/***************************************************************************
 *   Event log: part states and capacitor charge events, written to the    *
 *   Messages pane with their simulated time (and kept for the CLI)        *
 ***( see copyright.txt file at root folder )*******************************/

#include <QSettings>
#include <QtMath>
#include <algorithm>

#include "eventlog.h"
#include "statelog.h"
#include "statusview.h"
#include "chargeview.h"
#include "mainwindow.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "component.h"
#include "capacitorbase.h"
#include "scriptcpu.h"
#include "subcircuit.h"

static const uint64_t ms = 1000000000ull;          // Picoseconds
static const int maxLinesPerSec = 20;              // Per part, wall clock
static const int maxEvents = 20000;                // Kept for the CLI

EventLog* EventLog::self()
{
    static EventLog* log = new EventLog();
    return log;
}

EventLog::EventLog()
        : Updatable()
{
    QSettings* s = MainWindow::self()->settings();
    m_logParts = s->value("Circuit/logParts", true ).toBool();
    m_logCaps  = s->value("Circuit/logCaps",  true ).toBool();
    m_rateTimer.start();
}

void EventLog::setLogParts( bool l )
{
    m_logParts = l;
    MainWindow::self()->settings()->setValue("Circuit/logParts", l );
    CircuitWidget::self()->syncEventLog();
}

void EventLog::setLogCaps( bool l )
{
    m_logCaps = l;
    MainWindow::self()->settings()->setValue("Circuit/logCaps", l );
    CircuitWidget::self()->syncEventLog();
}

void EventLog::attach()
{
    if( Simulator::self() ) Simulator::self()->addToUpdateList( this ); // No-op if listed
    m_tracks.clear();
    m_events.clear();
    m_rateTimer.restart();
    m_partsWas = partsActive();                   // A new run logs from its start
    m_capsWas  = capsActive();
}

QString EventLog::timeStr( uint64_t ps )
{
    double t = (double)ps/1e12;
    QString s;
    if     ( t < 1e-3 ) s = QString::number( t*1e6, 'f', 3 )+" µs";
    else if( t < 1    ) s = QString::number( t*1e3, 'f', 3 )+" ms";
    else               s = QString::number( t,     'f', 4 )+" s ";
    return s.rightJustified( 11 );
}

//------------------------------------------------------------------------------

bool EventLog::partsActive() { return m_logParts && StatusView::self()->enabled(); }
bool EventLog::capsActive()  { return m_logCaps  && ChargeView::self()->enabled(); }

void EventLog::updateStep()
{
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ) return;

    solverReports();

    bool parts = partsActive(), caps = capsActive();
    bool partsOn = parts && !m_partsWas, capsOn = caps && !m_capsWas;   // Just switched on
    m_partsWas = parts;
    m_capsWas  = caps;
    if( !parts && !caps ) return;                 // Nothing to show: no work

    flushRate();
    uint64_t now = sim->circTime();
    for( Component* comp : *Circuit::self()->compList() )
    {
        if( parts ) partEvents( comp, partsOn );  // Switched on: from now, not the whole history
        if( !caps ) continue;
        if( CapacitorBase* cap = dynamic_cast<CapacitorBase*>( comp ) )
        {
            if( !cap->logCharge() ) continue;
            if( capsOn ) m_tracks.remove( cap->getUid() );   // Fresh samples
            capEvents( cap, now );
        }
    }
}

// An element id ("Diode-9", or "1@Zener-6" inside a subcircuit) as the user knows it: "D6", "IC1.ZVDD"
QString EventLog::elementLabel( QString id, QString* uid )
{
    for( Component* comp : *Circuit::self()->compList() )
    {
        if( comp->getUid() == id ) { if( uid->isEmpty() ) *uid = comp->getUid(); return comp->idLabel(); }
        if( SubCircuit* sub = dynamic_cast<SubCircuit*>( comp ) )
            for( Component* inner : *sub->compList() )
                if( inner->getUid() == id || inner->getUid() == id.section('@', -1 ) )
                {
                    if( uid->isEmpty() ) *uid = comp->getUid();
                    return comp->idLabel()+"."+inner->idLabel();
                }
    }
    return id;
}

void EventLog::solverReports()
{
    for( const Simulator::NlReport& r : Simulator::self()->takeNlReports() )
    {
        QString uid;
        QStringList names;
        for( const QString& id : r.ids ) names.append( elementLabel( id, &uid ) );
        names.sort();
        QString text = QString("Not converging after %1 iterations, step taken as it is; the next steps get at most 1000 "
                               "until one converges. Still iterating: %2").arg( r.iterations ).arg( names.join(", ") );
        if( m_events.size() >= maxEvents ) m_events.removeFirst();
        m_events.append({ r.time, false, "solver", uid, "solver", Component::stFault, text, true });
        CircuitWidget::self()->eventMessage( "[ "+timeStr( r.time )+"] "+text, Component::stFault, uid );
    }
}

void EventLog::stop()
{
    solverReports();
    if( partsActive() )
        for( Component* comp : *Circuit::self()->compList() ) partEvents( comp );  // Last frame
    m_partsWas = m_capsWas = false;
    for( Component* comp : *Circuit::self()->compList() )
    {
        if( !m_tracks.contains( comp->getUid() ) ) continue;
        Track& tr = m_tracks[ comp->getUid() ];
        if( !tr.cycling ) continue;
        uint64_t avg = tr.repeats > 1 ? (tr.lastEnd-tr.firstEnd)/(tr.repeats-1) : 0;
        write( comp, "part", { tr.lastEnd, false, Component::stOff,
               QString("still repeating its cycle at the stop: %1 times, every %2 (%3 Hz) on average")
               .arg( tr.repeats ).arg( timeStr( avg ).trimmed() ).arg( avg ? 1e12/avg : 0, 0, 'f', 2 ), "" }, true );
        tr.cycling = false;
    }
}

void EventLog::flushRate()
{
    if( m_rateTimer.elapsed() < 1000 ) return;
    m_rateTimer.restart();
    for( Component* comp : *Circuit::self()->compList() )
    {
        if( !m_tracks.contains( comp->getUid() ) ) continue;
        Track& tr = m_tracks[ comp->getUid() ];
        tr.lines = 0;
        if( tr.dropped == 0 ) continue;
        int n = tr.dropped;
        tr.dropped = 0;
        write( comp, "part", { Simulator::self()->circTime(), true, Component::stOff,
               QString("%1 more changes not shown (simulide cli log / status %2)").arg( n ).arg( comp->idLabel() ), "" }, true );
    }
}

// Part states: the setStatus history of the part's scripted cores, one line per moment
void EventLog::partEvents( Component* comp, bool silent )
{
    auto cores = StatusView::statusCores( comp );
    StateLog* own = comp->stateLog();             // A built-in part's operating state (BJT, MOSFET...)
    if( cores.isEmpty() && !own ) return;
    Track& tr = m_tracks[ comp->getUid() ];

    struct Ev { QString block; ScriptCpu::StatusEvent e; };
    QList<Ev> evs;
    uint64_t maxSeq = tr.seq;
    for( auto& core : cores )
        for( auto& e : core.second->statusHistory() )
            if( e.seq > tr.seq ) { evs.append({ core.first, e }); maxSeq = qMax( maxSeq, e.seq ); }
    if( own )
        for( auto& e : own->history() )
            if( e.seq > tr.seq ) { evs.append({ "", e }); maxSeq = qMax( maxSeq, e.seq ); }
    tr.seq = maxSeq;
    if( silent )                                  // Catch up: remember the states, no lines
    {
        for( auto& core : cores ) if( core.second->status().level >= 0 ) tr.last[ core.first ] = core.second->status().text;
        if( own && own->current().level >= 0 ) tr.last[""] = own->current().text;
        return;
    }
    if( evs.isEmpty() ) return;
    std::sort( evs.begin(), evs.end(), []( const Ev& a, const Ev& b ){ return a.e.seq < b.e.seq; } );

    int i = 0;
    while( i < evs.size() )                       // Changes within 1 µs: one line
    {
        uint64_t t0 = evs[i].e.time;
        QStringList order;
        QHash<QString, QStringList> chain;
        int level = -1;
        for( ; i < evs.size() && evs[i].e.time-t0 < 1000000; ++i )
        {
            const Ev& ev = evs[i];
            if( !chain.contains( ev.block ) ) order.append( ev.block );
            chain[ ev.block ].append( ev.e.text );
            level = qMax( level, ev.e.level );
        }
        QStringList parts, sigs;
        for( const QString& block : order )
        {
            QStringList steps = chain[ block ];
            QString from = tr.last.value( block );
            QString seg = ( from.isEmpty() ? "" : from+" → " )+steps.join(" → ");
            parts.append( block.isEmpty() ? seg : block+": "+seg );
            sigs.append( block+"="+steps.join(">") );
            tr.last[ block ] = steps.last();
        }
        emitLine( comp, "part", { t0, false, level, parts.join("  ·  "), sigs.join(";") } );
    }
}

// Capacitors with "Log charge": charging, charged, discharging, discharged, over the rating.
// Sampled once per frame, from 20 ms means (ripple averages out), with a 3 % hysteresis.
void EventLog::capEvents( CapacitorBase* cap, uint64_t now )
{
    enum { Empty=0, Charging, Steady, Discharging };
    Track& tr = m_tracks[ cap->getUid() ];
    double v = qAbs( cap->capVolt() );

    tr.samples.append({ now, v });
    while( !tr.samples.isEmpty() && tr.samples.first().first+60*ms < now ) tr.samples.removeFirst();
    tr.peak = qMax( tr.peak, v );
    double scale = qMax( tr.peak, 0.5 );

    double mv = cap->maxVolt();                   // Rated voltage
    if( mv > 0 && !tr.over && v > mv )
    {
        tr.over = true;
        emitLine( cap, "cap", { now, true, Component::stFault,
                  "above its "+ChargeView::voltStr( mv )+" rating: "+ChargeView::voltStr( v ), "over" } );
    }
    else if( tr.over && v < mv*0.98 )
    {
        tr.over = false;
        emitLine( cap, "cap", { now, true, Component::stOn, "back within its rating", "rated" } );
    }

    double s1 = 0, s0 = 0; int n1 = 0, n0 = 0;    // Means of the last 20 ms and the 20 ms before:
    for( auto& s : tr.samples )                   // two 100 Hz ripple periods, so ripple averages out
    {
        if     ( s.first+20*ms > now ) { s1 += s.second; n1++; }
        else if( s.first+40*ms > now ) { s0 += s.second; n0++; }
    }
    if( tr.capState < 0 ) tr.capState = ( v < 0.05*scale ) ? Empty : Steady;
    if( n1 == 0 || n0 == 0 ) return;              // Not 40 ms of history yet
    double m1 = s1/n1, m0 = s0/n0;
    if( tr.ref < 0 ) tr.ref = m1;

    // A move of 3 % of the capacitor's own scale away from the level of the last event (or the
    // extreme since, while it keeps going the same way) is an event; less is ripple or drift
    if     ( tr.capState == Charging    ) tr.ref = qMax( tr.ref, m1 );
    else if( tr.capState == Discharging ) tr.ref = qMin( tr.ref, m1 );
    double thr = qMax( 0.03*scale, 0.02 );

    QString what, sig; int level = Component::stOn;
    if( m1 > tr.ref+thr && tr.capState != Charging )
    {
        level = Component::stWait;
        sig  = QString("charging@%1").arg( qRound( 10*tr.ref/scale ) );   // Starting level in 10 % steps
        what = "charging from "+ChargeView::voltStr( tr.ref );
        tr.capState = Charging; tr.ref = m1;
    }
    else if( m1 < tr.ref-thr && tr.capState != Discharging && tr.capState != Empty )
    {
        level = Component::stOff;
        sig  = QString("discharging@%1").arg( qRound( 10*tr.ref/scale ) );
        what = "discharging from "+ChargeView::voltStr( tr.ref );
        tr.capState = Discharging; tr.ref = m1;
    }
    else if( tr.capState == Discharging && m1 < 0.05*scale )
    {
        level = Component::stOff; sig = "discharged"; what = "discharged";
        tr.capState = Empty; tr.ref = m1;
    }
    else if( qAbs( m1-m0 ) < 0.005*scale && ( tr.capState == Charging || tr.capState == Discharging ) )
    {
        sig  = "~";                               // A plateau: only seen if a frame catches it, not used for cycles
        what = ( tr.capState == Charging ? "charged to " : "steady at " )+ChargeView::voltStr( m1 );
        tr.capState = Steady; tr.ref = m1;
    }
    if( !what.isEmpty() ) emitLine( cap, "cap", { now, true, level, what, sig } );
}

// Repeating cycles are summarised: once the same sequence of changes has come three times, one line
// with its period, then nothing until it changes
void EventLog::emitLine( Component* comp, QString kind, const Line& line )
{
    Track& tr = m_tracks[ comp->getUid() ];
    if( line.sig == "~" )                         // Minor line: shown, but not part of a cycle
    {
        write( comp, kind, line, !tr.cycling );
        return;
    }
    if( tr.cycling )
    {
        if( line.sig == tr.pattern.at( tr.patIdx ) )
        {
            if( ++tr.patIdx == tr.pattern.size() ) { tr.patIdx = 0; tr.repeats++; tr.lastEnd = line.time; }
            write( comp, kind, line, false );
            return;
        }
        uint64_t avg = tr.repeats > 1 ? (tr.lastEnd-tr.firstEnd)/(tr.repeats-1) : 0;
        write( comp, kind, { line.time, line.approx, Component::stOff,
               QString("cycle ended after %1 repeats, every %2 (%3 Hz) on average")
               .arg( tr.repeats ).arg( timeStr( avg ).trimmed() ).arg( avg ? 1e12/avg : 0, 0, 'f', 2 ), "" }, true );
        tr.cycling = false;
        tr.sigs.clear(); tr.times.clear();
    }
    tr.sigs.append( line.sig );
    tr.times.append( line.time );
    while( tr.sigs.size() > 32 ) { tr.sigs.removeFirst(); tr.times.removeFirst(); }
    write( comp, kind, line, true );

    int n = tr.sigs.size();
    for( int p=1; p<=8 && 3*p<=n; ++p )
    {
        QStringList last = tr.sigs.mid( n-p, p );
        if( last != tr.sigs.mid( n-2*p, p ) || last != tr.sigs.mid( n-3*p, p ) ) continue;
        tr.cycling  = true;
        tr.pattern  = last;
        tr.patIdx   = 0;
        tr.repeats  = 3;
        tr.firstEnd = tr.times.at( n-1-2*p );
        tr.lastEnd  = tr.times.at( n-1 );
        uint64_t period = (tr.lastEnd-tr.firstEnd)/2;
        write( comp, kind, { line.time, line.approx, Component::stOff,
               QString("repeats the same %1 every %2 (%3 Hz): not listed further while it repeats")
               .arg( p == 1 ? "change" : QString("cycle of %1 changes").arg( p ) )
               .arg( timeStr( period ).trimmed() ).arg( period ? 1e12/period : 0, 0, 'f', 2 ), "" }, true );
        break;
    }
}

void EventLog::write( Component* comp, QString kind, const Line& line, bool shown )
{
    Track& tr = m_tracks[ comp->getUid() ];
    bool paneOn = ( kind == "part" ) ? partsActive() : capsActive();
    if( shown && paneOn )
    {
        if( tr.lines >= maxLinesPerSec ) { tr.dropped++; shown = false; }
        else tr.lines++;
    }
    if( m_events.size() >= maxEvents ) m_events.removeFirst();
    m_events.append({ line.time, line.approx, comp->idLabel(), comp->getUid(), kind, line.level, line.text, shown && paneOn });

    if( !shown || !paneOn ) return;
    QString text = "["+QString( line.approx ? "≈" : " " )+timeStr( line.time )+"] "+comp->idLabel()+"  "+line.text;
    CircuitWidget::self()->eventMessage( text, line.level, comp->getUid() );
}
