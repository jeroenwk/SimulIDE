/***************************************************************************
 *   Event log: part states and capacitor charge events, written to the    *
 *   Messages pane with their simulated time (and kept for the CLI)        *
 ***( see copyright.txt file at root folder )*******************************/

#include <QSettings>
#include <QtMath>
#include <algorithm>

#include "eventlog.h"
#include "statusview.h"
#include "chargeview.h"
#include "mainwindow.h"
#include "circuit.h"
#include "circuitwidget.h"
#include "simulator.h"
#include "component.h"
#include "capacitorbase.h"
#include "scriptcpu.h"

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

void EventLog::updateStep()
{
    Simulator* sim = Simulator::self();
    if( !sim || !sim->isRunning() ) return;

    flushRate();
    uint64_t now = sim->circTime();
    for( Component* comp : *Circuit::self()->compList() )
    {
        partEvents( comp );
        if( CapacitorBase* cap = dynamic_cast<CapacitorBase*>( comp ) )
            if( cap->logCharge() ) capEvents( cap, now );
    }
}

void EventLog::stop()
{
    for( Component* comp : *Circuit::self()->compList() ) partEvents( comp );  // Last frame
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
void EventLog::partEvents( Component* comp )
{
    auto cores = StatusView::statusCores( comp );
    if( cores.isEmpty() ) return;
    Track& tr = m_tracks[ comp->getUid() ];

    struct Ev { QString block; ScriptCpu::StatusEvent e; };
    QList<Ev> evs;
    uint64_t maxSeq = tr.seq;
    for( auto& core : cores )
        for( auto& e : core.second->statusHistory() )
            if( e.seq > tr.seq ) { evs.append({ core.first, e }); maxSeq = qMax( maxSeq, e.seq ); }
    tr.seq = maxSeq;
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
// Sampled once per frame: the 10 ms mean now against the 10 ms before (ripple averages out).
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

    double s1 = 0, s0 = 0; int n1 = 0, n0 = 0;
    for( auto& s : tr.samples )
    {
        if     ( s.first+10*ms > now ) { s1 += s.second; n1++; }
        else if( s.first+20*ms > now ) { s0 += s.second; n0++; }
    }
    if( tr.capState < 0 ) tr.capState = ( v < 0.05*scale ) ? Empty : Steady;
    if( n1 == 0 || n0 == 0 ) return;              // Not 20 ms of history yet
    double m1 = s1/n1, m0 = s0/n0, slope = m1-m0;
    double big = qMax( 0.01*scale, 0.005 );

    QString what, sig; int level = Component::stOn;
    if( slope > big && tr.capState != Charging )
    {
        tr.capState = Charging; level = Component::stWait;
        sig = QString("charging@%1").arg( qRound( 10*m0/scale ) );   // Starting level in 10 % steps
        what = "charging from "+ChargeView::voltStr( m0 );
    }
    else if( slope < -big && tr.capState != Discharging && tr.capState != Empty )
    {
        tr.capState = Discharging; level = Component::stOff;
        sig = QString("discharging@%1").arg( qRound( 10*m0/scale ) );
        what = "discharging from "+ChargeView::voltStr( m0 );
    }
    else if( tr.capState == Discharging && m1 < 0.05*scale )
    {
        tr.capState = Empty; level = Component::stOff; sig = "discharged";
        what = "discharged";
    }
    else if( qAbs( slope ) < 0.003*scale && ( tr.capState == Charging || tr.capState == Discharging ) )
    {
        sig  = "~";                               // A plateau: only seen if a frame catches it, not used for cycles
        what = ( tr.capState == Charging ? "charged to " : "steady at " )+ChargeView::voltStr( m1 );
        tr.capState = Steady;
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
    bool paneOn = ( kind == "part" ) ? m_logParts : m_logCaps;
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
